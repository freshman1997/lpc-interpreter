local n = 500000
local i = 0
local sum = 0
while i < n do
  local ok = pcall(function()
    sum = sum + 1
  end)
  if not ok then
    sum = sum - 1
  end
  i = i + 1
end
if sum <= 0 then os.exit(1) end
print(sum)
