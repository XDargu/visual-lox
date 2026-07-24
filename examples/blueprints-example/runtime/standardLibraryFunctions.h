#pragma once

#include <Value.h>

class VM;

Value MathAbs(int, Value*, VM*);
Value MathMin(int, Value*, VM*);
Value MathMax(int, Value*, VM*);
Value MathClamp(int, Value*, VM*);
Value MathPower(int, Value*, VM*);
Value MathSqrt(int, Value*, VM*);
Value MathFloor(int, Value*, VM*);
Value MathCeil(int, Value*, VM*);
Value MathRound(int, Value*, VM*);
Value MathRandom(int, Value*, VM*);

Value StringTrim(int, Value*, VM*);
Value StringReplace(int, Value*, VM*);
Value StringJoin(int, Value*, VM*);
Value StringStartsWith(int, Value*, VM*);
Value StringEndsWith(int, Value*, VM*);
Value StringFormat(int, Value*, VM*);
Value StringParseNumber(int, Value*, VM*);
Value StringParseBool(int, Value*, VM*);

Value ListInsert(int, Value*, VM*);
Value ListClear(int, Value*, VM*);
Value ListSlice(int, Value*, VM*);
Value ListReverse(int, Value*, VM*);
Value ListSort(int, Value*, VM*);
Value ListDistinct(int, Value*, VM*);
Value ListEnumerate(int, Value*, VM*);
Value ListZip(int, Value*, VM*);

Value RangeMakeAdvanced(int, Value*, VM*);

Value FileReadText(int, Value*, VM*);
Value FileWriteText(int, Value*, VM*);
Value FileAppendText(int, Value*, VM*);
Value FileListDirectory(int, Value*, VM*);
Value PathCombine(int, Value*, VM*);
Value PathExtension(int, Value*, VM*);
Value PathFilename(int, Value*, VM*);
Value PathParent(int, Value*, VM*);
