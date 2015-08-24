use myrecord;

proc make(i:int) {
  var ret: R;
  ret.init(x = i);
  ret.verify();
  return ret;
}

proc main() {
  var A = for i in 1..4 do make(i);

  iter myiter() {
    for r in A do yield r;
  }


  var i = 1;
  for r in myiter() {
    assert(r.x == i);
    i += 1;
  }

  verify();
}
