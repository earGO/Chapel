/*
 * Copyright 2004-2015 Cray Inc.
 * Other additional copyright holders may be indicated within.
 *
 * The entirety of this work is licensed under the Apache License,
 * Version 2.0 (the "License"); you may not use this file except
 * in compliance with the License.
 *
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "astutil.h"
#include "build.h"
#include "passes.h"
#include "scopeResolve.h"
#include "stmt.h"
#include "stlUtil.h"
#include "stringutil.h"
#include "CForLoop.h"
#include "GPUForLoop.h"
#include <iostream>

// Return true if the symbol is defined in an outer function to fn
// third argument not used at call site
static bool isOuterVar(Symbol* sym, FnSymbol* fn, Symbol* parent = NULL)
{
  if (!parent)
    parent = fn->defPoint->parentSymbol;
  if (!isFnSymbol(parent))
    return false;
  else if (sym->defPoint->parentSymbol == parent)
    return true;
  else
    return isOuterVar(sym, fn, parent->defPoint->parentSymbol);
}

// Return true if the for loop can be considered for gpu offload ie. if the
// enclosing function or any function in the call-chain for the enclosing fn
// has the flag FLAG_FIT_GOR_GPU set. A way to allow the programmer to 
// restrict what loops can be considered for GPU offload. TODO: this shuold
// go away once the code is sufficiently stable.
static bool isFitForGPUOffload(CForLoop *cForLoop)
{
  FnSymbol *fn = toFnSymbol(cForLoop->parentSymbol);
  while (fn) {
    if (fn->hasFlag(FLAG_FIT_FOR_GPU)) return true;
    FnSymbol* caller = NULL;
    forv_Vec(CallExpr, call, gCallExprs) {
      if (call->inTree()) {
        if (FnSymbol* cfn = call->isResolved()) {
          if (cfn == fn) {
            caller = toFnSymbol(call->parentSymbol);
            break;
          }
        }
      }
    }
    fn = caller;
  }
  return false;
}

// Find variables that are used inside but defined outside the function
static void findOuterVars(FnSymbol* fn, SymbolMap* uses)
{
  std::vector<BaseAST*> asts;
  collect_asts(fn, asts);

  for_vector(BaseAST, ast, asts) {
    if (SymExpr* symExpr = toSymExpr(ast)) {
      Symbol* sym = symExpr->var;
      if (isLcnSymbol(sym)) {
        if (isOuterVar(sym, fn))
          uses->put(sym, gNil);
      }
    }
  }
}

// Create a class with a field for every externally defined variable in the
// function (following the create_arg_bundle_class() function in parallel.cpp)
static AggregateType *
createArgBundleClass(SymbolMap& vars, ModuleSymbol *mod, FnSymbol *fn)
{
  AggregateType* ctype = new AggregateType(AGGREGATE_CLASS);
  TypeSymbol* new_c = new TypeSymbol(astr("_class_locals", fn->name), ctype);
  new_c->addFlag(FLAG_NO_OBJECT);
  new_c->addFlag(FLAG_NO_WIDE_CLASS);
//  new_c->addFlag(FLAG_OFFLOAD_TO_GPU);
  int i = 0;
  form_Map(SymbolMapElem, e, vars) {
    Symbol* sym = e->key;
    if (e->value != gVoid) {
      if (sym->type->symbol->hasFlag(FLAG_REF) || isClass(sym->type)) {
      // Only a variable that is passed by reference out of its current scope
      // is concurrently accessed -- which means that it has to be passed by
      // reference.
        sym->addFlag(FLAG_CONCURRENTLY_ACCESSED);
      }
      //call->insertAtTail(sym);
      VarSymbol* field = new VarSymbol(astr("_", istr(i), "_", sym->name),
          sym->type);
//      sym->type->symbol->addFlag(FLAG_OFFLOAD_TO_GPU);
      ctype->addDeclarations(new DefExpr(field), true);
      ++i;
    }
  }
  mod->block->insertAtHead(new DefExpr(new_c));
  return ctype;
}

// Set the fields in the bundled argument with the external symbols
static void setBundleFields(CallExpr* call, SymbolMap& vars,
                            AggregateType *ctype, VarSymbol *tempc)
{
  int i = 1;
  form_Map(SymbolMapElem, e, vars) {
    Symbol* sym = e->key;
    if (e->value != gVoid) {
      CallExpr *setc = new CallExpr(PRIM_SET_MEMBER,
          tempc,
          ctype->getField(i),
          sym);
      call->insertBefore(setc);
      i++;
    }
  }
}

// Update values for the symbols in the map with formal arguments
static void getNewSymbolsFromFormal(FnSymbol* fn, SymbolMap& vars,
                                    ArgSymbol *wrap_c,
                                    AggregateType *ctype)
{
  int i = 1;
  form_Map(SymbolMapElem, e, vars) {
      Symbol* sym = e->key;
      if (e->value != gVoid) {
        SET_LINENO(sym);
        INT_ASSERT(e->value == gNil);
        VarSymbol* tmp = newTemp(sym->name, sym->type);
        fn->insertAtHead(
          new CallExpr(PRIM_MOVE, tmp,
            new CallExpr(PRIM_GET_MEMBER_VALUE, wrap_c, ctype->getField(i))));
        fn->insertAtHead(new DefExpr(tmp));
        e->value = tmp;
        ++i;
      }
  }
}

// 1. For each of the gpu-targeted functions created inside the GPUForLoops,
// capture the external symbols for the function (defined outside and used
// inside) and bundle them into a single function argument. Pass the bundle
// as an argument to the function. Un-bundle into variables inside the
// function body, and replace the external variables with these new varables.
//
// 2. Move function to module level.
//
// For example:
// Original CForLoop:
// var i;
// for (i = k1; i < k2; i += k3) {
//  A[i] = B[i];
// }
//
// New GPUForLoop already built from the CForLoop: (see GPUForLoop.cpp)
// var i;
// var trip_count = (k2 - k1) / k3 + ((k2 - k1) % k3 != 0);
//
// //arg_bundle, wkgrp_size and trip_count are not codegened
// void gpu_func_1 (wkgrp_size, trip_count) {
//   var c_idx = call_primitive(GET_GLOBAL_ID);
//   var j = k1 + c_idx * k3;
//   A[j] = B[j];
// }
//
// GPUForLoop after bundling external variables in this function:
// var i;
// var trip_count = (k2 - k1) / k3 + ((k2 - k1) % k3 != 0);
//
// class Bundle {
//  a: A.type;
//  b: B.type;
// };
// Bundle arg_bundle;
// arg_bundle.a = A;
// arg_bundle.b = B
// void *dummy = arg_bundle;
//
// //arg_bundle, wkgrp_size and trip_count are not codegened
// void gpu_func_1 (dummy, arg_bundle, wkgrp_size, trip_count) {
//   var A = dummy.a;
//   var B = dummy.b;
//   var c_idx = call_primitive(GET_GLOBAL_ID);
//   var j = k1 + c_idx * k3;
//   A[j] = B[j];
// }
//

static void passExternalSymbols(FnSymbol *fn)
{

  SET_LINENO(fn);
  SymbolMap uses;
  findOuterVars(fn, &uses);

  // create type for the argument bundle
  ModuleSymbol* mod = fn->getModule();
  AggregateType *ctype = createArgBundleClass(uses, mod, fn);

  // add formal arguments
  // 1. Void Pointer to bundled args
  ArgSymbol *bundle_args = new ArgSymbol( INTENT_IN, "bundle", dtCVoidPtr);
  fn->insertFormalAtTail(bundle_args);
  bundle_args->addFlag(FLAG_NO_CODEGEN);

  // 2. Bundled args
  ArgSymbol *wrap_c = new ArgSymbol( INTENT_IN, "dummy_c", ctype);
  fn->insertFormalAtTail(wrap_c);

  // Update values for the symbols in the map with formal arguments
  getNewSymbolsFromFormal(fn, uses, wrap_c, ctype);

  // Replace external symbols with values in the symbol map
  replaceVarUses(fn->body, uses);

  // Update call sites with argument objects
  // There should be only a single caller per gpu-targeted function
  INT_ASSERT(fn->calledBy->n == 1);
  CallExpr *call = fn->calledBy->v[0];
  pruneThisArg(call->parentSymbol, uses); //TODO: is this needed?

  VarSymbol *tempc = newTemp(astr("_args_for_", fn->name), ctype);
  call->insertBefore(new DefExpr(tempc));
  insertChplHereAlloc(call, false, tempc, ctype, newMemDesc("bundled args"));

  // initialize the arg-bundle fields using outer varsymbols
  setBundleFields(call, uses, ctype, tempc);

  // Put the bundle into a void* argument
  VarSymbol *allocated_args = newTemp(astr("_args_vfor", fn->name), dtCVoidPtr);
  call->insertBefore(new DefExpr(allocated_args));
  call->insertBefore(new CallExpr(PRIM_MOVE, allocated_args,
        new CallExpr(PRIM_CAST_TO_VOID_STAR, tempc)));

  // add actual arguments
  // 1. void pointer to bundled args
  call->insertAtTail(allocated_args);
  // 2. bundled args
  call->insertAtTail(tempc);
}

// Move function to module level (denest).
static void flattenFns(FnSymbol *fn)
{
  ModuleSymbol* mod = fn->getModule();
  Expr* def = fn->defPoint;
  def->remove();
  mod->block->insertAtTail(def);
}

// The createGPUForLoops pass considers order-independent CForLoops and
// replaces them with a block of the following form:
//
// var _is_gpu = call_primitive(PRIM_IS_GPU_SUBLOCALE);
// if (_is_gpu) {
//   //...CForLoop block
// } else {
//   // GPUForLoop block
// }
//
// The GPUForLoop block is created from the CForLoop block (GPUForLoop.cpp)
// using a copy of the CForLoop block body.
// The GPUForLoop encapsulates this new block in a function that is later
// offloaded to the GPU. Any symbol external to this function is captured
// and passed as an argument (passExternalSymbols), and the function def
// is moved to module level.

void createGPUForLoops(void)
{
  std::vector<BlockStmt *> blockStmts;
  forv_Vec(BlockStmt, block, gBlockStmts) {
    blockStmts.push_back(block);
  }
  for_vector(BlockStmt, block, blockStmts) {
    if (block->isCForLoop()) {
      CForLoop *cForLoop = toCForLoop(block);
      if (cForLoop->isOrderIndependent() && isFitForGPUOffload(cForLoop)) {

        SET_LINENO(block);

        GPUForLoop *gpuForLoop = GPUForLoop::buildFromIfPossible(cForLoop);
        
        if (fReportGPUForLoops) {
          ModuleSymbol *mod = toModuleSymbol(cForLoop->getModule());
          INT_ASSERT(mod);
          if (developer || mod->modTag == MOD_USER) {
            if (gpuForLoop) {
              printf("Replacing CForLoop with C and GPU for loops at %s:%d\n",
                  mod->name, cForLoop->linenum());
            } else {
              printf("Failed to generate GPUForLoop from CForLoop at %s:%d\n",
                  mod->name, cForLoop->linenum());
            }
          }
        }

        if (gpuForLoop) {
          CForLoop *cpuforLoop = cForLoop->copy();
          BlockStmt *condBlock = new BlockStmt();
          VarSymbol* isGpu = newTemp("_is_gpu", dtBool);
          condBlock->insertAtTail(new DefExpr(isGpu));
          condBlock->insertAtTail(new CallExpr(
                PRIM_MOVE, isGpu, new CallExpr(PRIM_IS_GPU_SUBLOCALE)));
          condBlock->insertAtTail(new CondStmt(
                new SymExpr(isGpu), gpuForLoop, cpuforLoop));
          cForLoop->replace(condBlock);
        }
      }
    }
  }

  //Convert external symbols to explicit arguments.
  std::vector<FnSymbol*> gpuFunctions;
  forv_Vec(FnSymbol, fn, gFnSymbols) {
    if (fn->hasFlag(FLAG_OFFLOAD_TO_GPU) && isAlive(fn))
      gpuFunctions.push_back(fn);
  }
  compute_call_sites();
  for_vector(FnSymbol, fn, gpuFunctions) {
    passExternalSymbols(fn);
    flattenFns(fn);
  }
}  // createGPUForLoops()

