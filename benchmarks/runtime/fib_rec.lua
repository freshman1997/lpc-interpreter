local function fib(n)
  if n <= 1 then return n end
  return fib(n - 1) + fib(n - 2)
end

local out = fib(24)
if out <= 0 then os.exit(1) end
print(out)
