config = {
	title = "My Game Engine",
	fullscreen = false,
	resolution = {
		width = 800,
		height = 600
	}
}

factorial = function(n)
	if n == 1 then
		return 1
	end
	return n * factorial(n-1);
end

print(cube(3))