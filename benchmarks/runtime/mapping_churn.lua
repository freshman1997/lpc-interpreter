local n = 100000
local i = 0
local sum = 0
while i < n do
  local m = {}
  m[i] = i + 1
  m[i + 100] = i + 2
  sum = sum + m[i]
  i = i + 1
end
if sum <= 0 then os.exit(1) end
print(sum)
