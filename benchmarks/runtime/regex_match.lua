local n = 50000
local i = 0
local sum = 0
while i < n do
  local ok = string.find("hello-123", "[a-z]+%-[0-9]+")
  if ok then sum = sum + 1 end
  local s = string.gsub("abc-123-xyz", "[0-9]+", "N")
  sum = sum + #s
  i = i + 1
end
if sum <= 0 then os.exit(1) end
print(sum)
