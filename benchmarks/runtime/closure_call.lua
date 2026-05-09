local n = 500000
local i = 0
local sum = 0
local offset = 10
local function fn(a)
  offset = offset + a
  return offset
end
while i < n do
  sum = sum + fn(i)
  i = i + 1
end
if sum <= 0 then os.exit(1) end
print(sum)
