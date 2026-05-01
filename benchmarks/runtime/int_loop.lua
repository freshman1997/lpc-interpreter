local n = 2000000
local i = 0
local sum = 0
while i < n do
  sum = sum + i
  i = i + 1
end
if sum <= 0 then os.exit(1) end
print(sum)
