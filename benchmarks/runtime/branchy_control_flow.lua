local n = 1500000
local i = 0
local sum = 0
while i < n do
  local k = i & 3
  if k == 0 then
    sum = sum + 3
  elseif k == 1 then
    sum = sum + 7
  elseif k == 2 then
    sum = sum + 11
  else
    sum = sum + 13
  end
  if (i & 15) == 0 then
    sum = sum - 1
  elseif (i & 7) == 0 then
    sum = sum + 2
  end
  i = i + 1
end
if sum <= 0 then os.exit(1) end
print(sum)
