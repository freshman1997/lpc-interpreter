local n = 100000
local i = 0
local sum = 0
while i < n do
  local arr = { i, i + 1, i + 2, i + 3, i + 4 }
  for _, item in ipairs(arr) do
    sum = sum + item
  end
  i = i + 1
end
if sum <= 0 then os.exit(1) end
print(sum)
