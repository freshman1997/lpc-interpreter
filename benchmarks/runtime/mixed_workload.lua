local n = 200000
local i = 0
local score = 0
local mode = "a"
while i < n do
  local m = { [i] = i + 1, [i + 10] = i + 2 }
  local v = m[i] or 0
  score = score + v
  if (i & 7) == 0 and mode == "a" then
    score = score + 1
  end
  i = i + 1
end
if score <= 0 then os.exit(1) end
print(score)
