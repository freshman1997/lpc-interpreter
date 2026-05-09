local n = 1000000
local i = 0
local pi = 3.14159265
local sum = 0.0
while i < n do
  local x = i * pi
  sum = sum + x / (x + 1.0)
  i = i + 1
end
if sum <= 0.0 then os.exit(1) end
print(sum)
