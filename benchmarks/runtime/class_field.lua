local n = 500000
local i = 0
local sum = 0
while i < n do
  local v = { x = 0, y = 0 }
  v.x = i
  v.y = i + 1
  sum = sum + v.x + v.y
  i = i + 1
end
if sum <= 0 then os.exit(1) end
print(sum)
