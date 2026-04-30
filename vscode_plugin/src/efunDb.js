"use strict";

const EFUN_DB = {
  call_other:     { args: ["object ob", "string func"], ret: "mixed", doc: "Call a function on another object by name." },
  print:          { args: ["mixed ..."], ret: "void", doc: "Print values to stdout without newline." },
  puts:           { args: ["mixed value"], ret: "void", doc: "Print a value to stdout with newline." },
  sleep:          { args: ["int ms"], ret: "void", doc: "Sleep for the given number of milliseconds." },
  sizeof:         { args: ["mixed value"], ret: "int", doc: "Return the size of a string, array, or mapping." },
  random:         { args: ["int n"], ret: "int", doc: "Return a random integer in [0, n)." },
  keys:           { args: ["mapping m"], ret: "array", doc: "Return an array of all keys in the mapping." },
  values:         { args: ["mapping m"], ret: "array", doc: "Return an array of all values in the mapping." },
  typeof:         { args: ["mixed value"], ret: "int", doc: "Return the type tag of a value as an integer." },
  to_string:      { args: ["mixed value"], ret: "string", doc: "Convert a value to its string representation." },
  to_int:         { args: ["mixed value"], ret: "int", doc: "Convert a value to an integer." },
  this_object:    { args: [], ret: "object", doc: "Return the current object reference." },
  clone_object:   { args: ["string path"], ret: "object", doc: "Clone an object from the given blueprint path." },
  destruct:       { args: ["object ob"], ret: "void", doc: "Destroy the given object." },
  sprintf:        { args: ["string fmt", "mixed ..."], ret: "string", doc: "Format a string using printf-style format specifiers." },
  write:          { args: ["mixed value"], ret: "void", doc: "Write a value to stdout." },
  time:           { args: [], ret: "int", doc: "Return the current Unix timestamp in seconds." },
  member_array:   { args: ["mixed item", "mixed array_or_string"], ret: "int", doc: "Find the index of item in array or substring in string. Returns -1 if not found." },
  explode:        { args: ["string str", "string delim"], ret: "array", doc: "Split a string by delimiter into an array." },
  implode:        { args: ["array arr", "string delim"], ret: "string", doc: "Join array elements with delimiter into a string." },
  stringp:        { args: ["mixed value"], ret: "int", doc: "Return 1 if the value is a string, 0 otherwise." },
  intp:           { args: ["mixed value"], ret: "int", doc: "Return 1 if the value is an integer, 0 otherwise." },
  floatp:         { args: ["mixed value"], ret: "int", doc: "Return 1 if the value is a float, 0 otherwise." },
  arrayp:         { args: ["mixed value"], ret: "int", doc: "Return 1 if the value is an array, 0 otherwise." },
  mappingp:       { args: ["mixed value"], ret: "int", doc: "Return 1 if the value is a mapping, 0 otherwise." },
  objectp:        { args: ["mixed value"], ret: "int", doc: "Return 1 if the value is an object, 0 otherwise." },
  nullp:          { args: ["mixed value"], ret: "int", doc: "Return 1 if the value is nil/null, 0 otherwise." },
  functionp:      { args: ["mixed value"], ret: "int", doc: "Return 1 if the value is a function/closure, 0 otherwise." },
  to_float:       { args: ["mixed value"], ret: "float", doc: "Convert a value to a float." },
  abs:            { args: ["int n"], ret: "int", doc: "Return the absolute value of an integer." },
  strlen:         { args: ["string s"], ret: "int", doc: "Return the length of a string." },
  map_delete:     { args: ["mapping m", "mixed key"], ret: "void", doc: "Delete a key from a mapping." },
  capitalize:     { args: ["string s"], ret: "string", doc: "Capitalize the first character of a string." },
  lower_case:     { args: ["string s"], ret: "string", doc: "Convert a string to lowercase." },
  upper_case:     { args: ["string s"], ret: "string", doc: "Convert a string to uppercase." },
  allocate:       { args: ["int size"], ret: "array", doc: "Allocate an array of the given size, filled with 0." },
  reverse:        { args: ["array arr"], ret: "array", doc: "Return a reversed copy of the array." },
  min:            { args: ["int a", "int b"], ret: "int", doc: "Return the minimum of two integers." },
  max:            { args: ["int a", "int b"], ret: "int", doc: "Return the maximum of two integers." },
  sqrt:           { args: ["int|float n"], ret: "int", doc: "Return the square root, truncated to integer." },
  ctime:          { args: ["int timestamp"], ret: "string", doc: "Convert a Unix timestamp to a human-readable string." },
  strsrch:        { args: ["string haystack", "string needle"], ret: "int", doc: "Search for a substring. Returns index or -1 if not found." },
  replace_string: { args: ["string str", "string from", "string to"], ret: "string", doc: "Replace all occurrences of 'from' with 'to' in a string." },
  sort_array:     { args: ["array arr"], ret: "array", doc: "Return a sorted copy of the array." },
  instanceof:     { args: ["object ob", "string|int class"], ret: "int", doc: "Check if an object is an instance of the given class. Returns 1 or 0." },
};

const EFUN_NAMES = Object.keys(EFUN_DB);

const LPC_KEYWORDS = [
  "if", "else", "for", "foreach", "in", "while", "do",
  "switch", "case", "default", "break", "continue", "return",
  "catch", "inherit", "fun", "var", "class", "new",
  "static", "public", "private", "nomask",
  "true", "false"
];

const LPC_TYPES = [
  "void", "int", "float", "string", "object", "mapping", "mixed", "function"
];

module.exports = { EFUN_DB, EFUN_NAMES, LPC_KEYWORDS, LPC_TYPES };
