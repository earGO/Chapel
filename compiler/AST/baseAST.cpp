#include "analysis.h"
#include "astutil.h"
#include "baseAST.h"
#include "expr.h"
#include "stmt.h"
#include "stringutil.h"
#include "symbol.h"
#include "symscope.h"
#include "type.h"
#include "yy.h"
#include "../passes/runAnalysis.h"
#include "../traversals/fixup.h"


static long uid = 1;

// This is here so that we can break on the creation of a particular
// BaseAST instance in gdb.
static void checkid(long id) {
}

long BaseAST::getNumIDs(void) {
  return uid;
}

BaseAST::BaseAST(void) {
  INT_FATAL("Base AST must be given a type");
}

BaseAST::BaseAST(astType_t type) :
  astType(type),
  id(uid++),
  prev(NULL),
  next(NULL),
  parentScope(NULL),
  parentSymbol(NULL),
  filename(yyfilename), 
  lineno(yystartlineno),
  traversalInfo(NULL),
  copyInfo(NULL)
{
  checkid(id);
  if (lineno == -1) {
    if (currentTraversal) {
      traversalInfo = stringcpy(currentTraversal);
    }
    if (currentLineno && currentFilename) {
      lineno = currentLineno;
      filename = stringcpy(currentFilename);
    }
  }
}


BaseAST*
BaseAST::copyInner(ASTMap* map) {
  INT_FATAL(this, "copyInner not implemented for BaseAST subclass");
  return NULL;
}


void BaseAST::traverse(Traversal* traversal, bool atTop) {
  INT_FATAL(this, "Cannot traverse BaseAST");
}


void BaseAST::traverseDef(Traversal* traversal, bool atTop) {
  INT_FATAL(this, "Cannot traverse definitions of BaseAST");
}


void BaseAST::print(FILE* outfile) {
  if (!this) {
    INT_FATAL("Calling print() on a Null AST");
  } else {
    INT_FATAL("print() is unimplemented for a subclass of BaseAST");
  }
}


void BaseAST::printDef(FILE* outfile) {
  INT_FATAL("printDef() is unimplemented for a subclass of BaseAST");
}


void BaseAST::println(FILE* outfile) {
  print(outfile);
  fprintf(outfile, "\n");
}


void BaseAST::codegen(FILE* outfile) {
  if (!this) {
    INT_FATAL("Calling codegen() on a Null AST");
  } else {
    INT_FATAL("codegen() is unimplemented for a subclass of BaseAST");
  }
}


void BaseAST::replaceChild(BaseAST* old_ast, BaseAST* new_ast) {
  INT_FATAL(this, "Unexpected call to BaseAST::replaceChild(old, new)");
}


void BaseAST::callReplaceChild(BaseAST* new_ast) {
  INT_FATAL(this, "Unexpected call to BaseAST::callReplaceChild(new)");
}


ASTContext BaseAST::getContext(void) {
  ASTContext context;
  INT_FATAL(this, "Unexpected call to BaseAST::getContext()");
  return context;
}


BaseAST* BaseAST::remove(void) {
  if (!this)
    return this;
  if (prev || next) {
    if (!prev || !next) {
      INT_FATAL("Ill-formed list in BaseAST::remove");
    }
    next->prev = prev;
    prev->next = next;
    next = NULL;
    prev = NULL;
  } else {
    callReplaceChild(NULL);
  }
  removeHelper(this);
  return this;
}


void BaseAST::replace(BaseAST* new_ast) {
  if (new_ast->parentSymbol) {
    INT_FATAL(new_ast, "Argument is already in AST in BaseAST::replace");
  }
  if (new_ast->prev || new_ast->next) {
    INT_FATAL(new_ast, "Argument is in a list in BaseAST::replace");
  }
  if (prev || next) {
    if (!prev || !next) {
      INT_FATAL(this, "Ill-formed list in BaseAST::replace");
    }
    new_ast->prev = prev;
    new_ast->next = next;
    next->prev = new_ast;
    prev->next = new_ast;
    next = NULL;
    prev = NULL;
  } else {
    callReplaceChild(new_ast);
  }
  ASTContext context = getContext();
  removeHelper(this);
  insertHelper(new_ast, context);
}


bool BaseAST::isList(void) {
  return false;
}


void BaseAST::insertBeforeListHelper(BaseAST* ast) {
  INT_FATAL(this, "Illegal call to insertAfterListHelper");
}


void BaseAST::insertAfterListHelper(BaseAST* ast) {
  INT_FATAL(this, "Illegal call to insertAfterListHelper");
}


void BaseAST::insertBefore(BaseAST* new_ast) {
  if (new_ast->parentSymbol) {
    INT_FATAL(new_ast, "Argument is already in AST in BaseAST::insertBefore");
  }
  if (isList()) {
    INT_FATAL(this, "Cannot call insertBefore on a list, use insertAtHead");
  }
  if (!prev) {
    INT_FATAL(this, "Cannot call insertBefore on BaseAST not in a list");
  }
  if (new_ast->prev || new_ast->next) {
    INT_FATAL(new_ast, "Argument is in a list in BaseAST::insertBefore");
  }
  if (dynamic_cast<Stmt*>(this) && dynamic_cast<Expr*>(new_ast)) {
    new_ast = new ExprStmt(dynamic_cast<Expr*>(new_ast));
  }
  if (new_ast->isList()) {
    new_ast->insertBeforeListHelper(this);
  } else {
    new_ast->prev = prev;
    new_ast->next = this;
    prev->next = new_ast;
    prev = new_ast;
    insertHelper(new_ast, getContext());
  }
}


void BaseAST::insertAfter(BaseAST* new_ast) {
  if (new_ast->parentSymbol) {
    INT_FATAL(new_ast, "Argument is already in AST in BaseAST::insertAfter");
  }
  if (isList()) {
    INT_FATAL(this, "Cannot call insertAfter on a list, use insertAtTail");
  }
  if (!next) {
    INT_FATAL(this, "Cannot call insertAfter on BaseAST not in a list");
  }
  if (new_ast->prev || new_ast->next) {
    INT_FATAL(new_ast, "Argument is in a list in BaseAST::insertAfter");
  }
  if (dynamic_cast<Stmt*>(this) && dynamic_cast<Expr*>(new_ast)) {
    new_ast = new ExprStmt(dynamic_cast<Expr*>(new_ast));
  }
  if (new_ast->isList()) {
    new_ast->insertAfterListHelper(this);
  } else {
    new_ast->prev = this;
    new_ast->next = next;
    next->prev = new_ast;
    next = new_ast;
    insertHelper(new_ast, getContext());
  }
}


void BaseAST::preCopy(ASTMap*& map,
                      Vec<BaseAST*>* update_list,
                      bool internal) {
  if (!map) {
    map = new ASTMap();
  }
}


void BaseAST::postCopy(BaseAST* copy,
                       ASTMap* map,
                       Vec<BaseAST*>* update_list,
                       bool internal) {
  copy->lineno = lineno;
  copy->filename = filename;
  copy->addPragmas(&pragmas);
  if (!inAnalysis) {
    Expr* expr = dynamic_cast<Expr*>(this);
    Expr* exprCopy = dynamic_cast<Expr*>(copy);
    if (expr && exprCopy) {
      exprCopy->ainfo = expr->ainfo;
    }
    Stmt* stmt = dynamic_cast<Stmt*>(this);
    Stmt* stmtCopy = dynamic_cast<Stmt*>(copy);
    if (stmt && stmtCopy) {
      stmtCopy->ainfo = stmt->ainfo;
    }
  }
  map->put(this, copy);
  if (!internal) {
    if (update_list) {
      for (int j = 0; j < update_list->n; j++) {
        for (int i = 0; i < map->n; i++) {
          if (update_list->v[j] == map->v[i].key) {
            update_list->v[j] = map->v[i].value;
          }
        }
      }
    }
    update_symbols(copy, map);
  }
}


char* BaseAST::stringLoc(void) {
  const int tmpBuffSize = 64;
  char tmpBuff[tmpBuffSize];

  snprintf(tmpBuff, tmpBuffSize, "%s:%d", filename, lineno);
  return stringcpy(tmpBuff);
}


void BaseAST::printLoc(FILE* outfile) {
  fprintf(outfile, "%s:%d", filename, lineno);
}


char* BaseAST::hasPragma(char* str) {
  forv_Vec(char, pragma, pragmas) {
    if (!strncmp(pragma, str, strlen(str)))
      return pragma;
  }
  if (!dynamic_cast<ModuleSymbol*>(this) && getModule())
    return getModule()->hasPragma(str);
  return NULL;
}


void BaseAST::addPragma(char* str) {
  if (ExprStmt* exprStmt = dynamic_cast<ExprStmt*>(this)) {
    exprStmt->expr->addPragma(str);
  } else if (DefExpr* defExpr = dynamic_cast<DefExpr*>(this)) {
    defExpr->sym->addPragma(str);
  } else {
    pragmas.add(stringcpy(str));
  }
}


void BaseAST::addPragmas(Vec<char*>* srcPragmas) {
  if (srcPragmas) {
    forv_Vec(char, srcPragma, *srcPragmas) {
      addPragma(srcPragma);
    }
  }
}


ModuleSymbol* BaseAST::getModule() {
  if (!this || dynamic_cast<UnresolvedSymbol*>(this))
    return NULL;
  if (ModuleSymbol* x = dynamic_cast<ModuleSymbol*>(this))
    return x;
  else if (Type* x = dynamic_cast<Type*>(this))
    return x->symbol->getModule();
  else if (Symbol* x = dynamic_cast<Symbol*>(this))
    return x->defPoint->getModule();
  else
    return parentSymbol->getModule();
}


FnSymbol* BaseAST::getFunction() {
  if (!this || dynamic_cast<UnresolvedSymbol*>(this))
    return NULL;
  if (ModuleSymbol* x = dynamic_cast<ModuleSymbol*>(this))
    return x->initFn;
  else if (FnSymbol* x = dynamic_cast<FnSymbol*>(this))
    return x;
  else if (Type* x = dynamic_cast<Type*>(this))
    return x->symbol->getFunction();
  else if (Symbol* x = dynamic_cast<Symbol*>(this))
    return x->defPoint->getFunction();
  else
    return parentSymbol->getFunction();
}


int compar_baseast(const void *ai, const void *aj) {
  BaseAST *i = *(BaseAST**)ai;
  BaseAST *j = *(BaseAST**)aj;
  if (i->id > j->id)
    return 1;
  else if (i->id < j->id)
    return -1;
  return 0;
}

char* astTypeName[AST_TYPE_END+1] = {
  "Stmt",
  "ExprStmt",
  "ReturnStmt",
  "BlockStmt",
  "ForLoopStmt",
  "CondStmt",
  "WhenStmt",
  "SelectStmt",
  "LabelStmt",
  "GotoStmt",

  "Expr",
  "SymExpr",
  "DefExpr",
  "CallExpr",
  "CastExpr",
  "NamedExpr",
  "ImportExpr",

  "Symbol",
  "UnresolvedSymbol",
  "ModuleSymbol",
  "VarSymbol",
  "ArgSymbol",
  "TypeSymbol",
  "FnSymbol",
  "EnumSymbol",
  "LabelSymbol",

  "Type",
  "PrimitiveType",
  "FnType",
  "EnumType",
  "UserType",
  "ClassType",
  "MetaType",
  "SumType",
  "VariableType",

  "List",

  "AST_TYPE_END"
};

int currentLineno = 0;
char* currentFilename = NULL;
char* currentTraversal = NULL;

#define AST_ADD_CHILD(_t, _m) if (((_t*)a)->_m) asts.add(((_t*)a)->_m)
#define AST_ADD_LIST(_t, _m, _mt) \
  for (_mt* tmp = ((_t*)a)->_m->first(); tmp; tmp = ((_t*)a)->_m->next()) \
      asts.add(tmp)
#define AST_ADD_VEC(_t, _m, _mt) \
    forv_Vec(_mt, c, ((_t*)a)->_m) asts.add(c)

#define ADD_CHILD(_t, _m) if (all) AST_ADD_CHILD(_t, _m)
#define ADD_LIST(_t, _m, _mt) if (all) AST_ADD_LIST(_t, _m, _mt)
#define ADD_VEC(_t, _m, _mt) if (all) AST_ADD_VEC(_t, _m, _mt)

void
get_ast_children(BaseAST *a, Vec<BaseAST *> &asts, int all) {
  switch (a->astType) {
  case STMT:
  LStmtCommon:
    break;
  case STMT_EXPR:
  LExprStmtCommon:
    AST_ADD_CHILD(ExprStmt, expr);
    goto LStmtCommon;
  case STMT_RETURN:
    goto LExprStmtCommon;
  case STMT_BLOCK:
    AST_ADD_LIST(BlockStmt, body, Stmt);
    goto LStmtCommon;
  case STMT_FORLOOP:
    AST_ADD_LIST(ForLoopStmt, indices, DefExpr);
    AST_ADD_LIST(ForLoopStmt, iterators, Expr);
    AST_ADD_CHILD(ForLoopStmt, innerStmt);
    goto LStmtCommon;
  case STMT_COND:
    AST_ADD_CHILD(CondStmt, condExpr);
    AST_ADD_CHILD(CondStmt, thenStmt);
    AST_ADD_CHILD(CondStmt, elseStmt);
    goto LStmtCommon;
  case STMT_WHEN:
    AST_ADD_LIST(WhenStmt, caseExprs, Expr);
    AST_ADD_CHILD(WhenStmt, doStmt);
    goto LStmtCommon;
  case STMT_SELECT:
    AST_ADD_CHILD(SelectStmt, caseExpr);
    AST_ADD_LIST(SelectStmt, whenStmts, WhenStmt);
    goto LStmtCommon;
  case STMT_LABEL:
    AST_ADD_CHILD(LabelStmt, defLabel);
    AST_ADD_CHILD(LabelStmt, stmt);
    goto LStmtCommon;
  case STMT_GOTO:
    ADD_CHILD(GotoStmt, label);
    goto LStmtCommon;
  case EXPR:
    LExprCommon:
    break;
  case EXPR_SYM:
    ADD_CHILD(SymExpr, var);
    goto LExprCommon;
  case EXPR_IMPORT:
    ADD_CHILD(ImportExpr, expr);
    goto LExprCommon;
  case EXPR_DEF:
    ADD_CHILD(DefExpr, sym);
    AST_ADD_CHILD(DefExpr, init);
    AST_ADD_CHILD(DefExpr, exprType);
    goto LExprCommon;
  case EXPR_CALL:
    AST_ADD_CHILD(CallExpr, baseExpr);
    AST_ADD_LIST(CallExpr, argList, Expr);
    goto LExprCommon;
  case EXPR_CAST:
    ADD_CHILD(CastExpr, type);
    AST_ADD_CHILD(CastExpr, newType);
    AST_ADD_CHILD(CastExpr, expr);
    goto LExprCommon;
  case EXPR_NAMED:
    AST_ADD_CHILD(NamedExpr, actual);
    goto LExprCommon;
  case SYMBOL: case SYMBOL_UNRESOLVED: 
  LSymbolCommon:
    ADD_CHILD(Symbol, type);
    break;
  case SYMBOL_MODULE:
    ADD_LIST(ModuleSymbol, stmts, Stmt);
    ADD_CHILD(ModuleSymbol, initFn);
    goto LSymbolCommon;
  case SYMBOL_VAR: 
    goto LSymbolCommon;
  case SYMBOL_ARG: 
    AST_ADD_CHILD(ArgSymbol, defaultExpr);
    ADD_CHILD(ArgSymbol, genericSymbol);
    goto LSymbolCommon;
  case SYMBOL_TYPE: goto LSymbolCommon;
  case SYMBOL_FN:
    ADD_LIST(FnSymbol, formals, DefExpr);
    AST_ADD_CHILD(FnSymbol, body);
    ADD_CHILD(FnSymbol, retType);
    ADD_CHILD(FnSymbol, _this);
    ADD_CHILD(FnSymbol, _setter);
    ADD_CHILD(FnSymbol, _getter);
    AST_ADD_CHILD(FnSymbol, whereExpr);
    goto LSymbolCommon;
  case SYMBOL_ENUM: case SYMBOL_LABEL:
    goto LSymbolCommon;
  case TYPE:
  LTypeCommon:
    ADD_CHILD(Type, symbol);
    ADD_CHILD(Type, defaultValue);
    ADD_CHILD(Type, defaultConstructor);
    ADD_CHILD(Type, metaType);
    break;
  case TYPE_PRIMITIVE:
  case TYPE_FN:
    goto LTypeCommon;
  case TYPE_ENUM:
    ADD_LIST(EnumType, constants, DefExpr);
    goto LTypeCommon;
  case TYPE_LITERAL:
    AST_ADD_CHILD(LiteralType, literal);
    goto LTypeCommon;
  case TYPE_USER:
    AST_ADD_CHILD(UserType, typeExpr);
    AST_ADD_CHILD(UserType, defaultExpr);
    ADD_CHILD(UserType, underlyingType);
    goto LTypeCommon;
  case TYPE_CLASS:
    ADD_LIST(ClassType, declarationList, Stmt);
    ADD_VEC(ClassType, fields, Symbol);
    ADD_VEC(ClassType, types, TypeSymbol);
    ADD_CHILD(ClassType, fieldSelector);
    goto LTypeCommon;
  case TYPE_META:
    ADD_CHILD(MetaType, base);
    goto LTypeCommon;
  case TYPE_SUM:
    ADD_VEC(SumType, components, Type);
    goto LTypeCommon;
  case TYPE_VARIABLE:
    ADD_CHILD(VariableType, type);
    goto LTypeCommon;
  case AST_TYPE_END: break;
  case LIST: 
    INT_FATAL(a, "Unexpected case in AST_GET_CHILDREN (LIST)");
    break;
  case OBJECT: 
    INT_FATAL(a, "Unexpected case in AST_GET_CHILDREN (OBJECT)");
    break;
  }
}
#undef AST_ADD_CHILD
#undef AST_ADD_LIST
#undef AST_ADD_VEC
#undef ADD_CHILD
#undef ADD_LIST
#undef ADD_VEC

void
collect_ast_children(BaseAST *a, Accum<BaseAST *> &acc, int all) {
  acc.add(a);
  forv_BaseAST(x, acc.asvec) {
    Vec<BaseAST *> tmp;
    get_ast_children(x, tmp, all);
    acc.add(tmp);
  }
}


