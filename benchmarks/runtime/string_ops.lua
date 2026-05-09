local n = 100000
local i = 0
local sum = 0
local base = "the quick brown fox jumps over the lazy dog"
while i < n do
  local pos = string.find(base, "fox", 1, true)
  if pos then sum = sum + pos end
  local s = string.gsub(base, "fox", "cat")
  sum = sum + #s
  i = i + 1
end
if sum <= 0 then os.exit(1) end
print(sum)
