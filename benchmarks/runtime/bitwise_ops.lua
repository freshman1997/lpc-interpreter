local n = 2000000
local i = 0
local sum = 0
while i < n do
  local a = i & 0xFF
  local b = a | 0x10
  local c = b ~ 0x55
  local d = c << 2
  local e = d >> 1
  sum = sum + e
  i = i + 1
end
if sum <= 0 then os.exit(1) end
print(sum)
