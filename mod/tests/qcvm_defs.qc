/*
Builtins for gmqcc's "qcvm" test runner. The numbers are qcvm's, not
Quake's: this file only exists so loot_core.qc can run outside the game.
*/
void(string s, ...) print = #1;
string(float f) ftos = #2;
string(string s1, ...) strcat = #10;
float(float f) floor = #14;
