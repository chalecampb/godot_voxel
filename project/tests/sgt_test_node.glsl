uniform float amplitude;
uniform int bounds_mode;

void generate(float x, float z, out float height) {
	height = amplitude * (sin(x) + cos(z));
}
