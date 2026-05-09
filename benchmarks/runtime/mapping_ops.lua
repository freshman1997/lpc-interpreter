local n = 100000
local i = 0
local sum = 0
while i < n do
  local m = {}
  m[i] = i + 1
  m[i + 100] = i + 2
  m[i + 200] = i + 3
  m[i + 300] = i + 4
  sum = sum + m[i]
  local sz = 0
  for _ in pairs(m) do sz = sz + 1 end
  sum = sum + sz
  i = i + 1
end
if sum <= 0 then os.exit(1) end
print(sum)
