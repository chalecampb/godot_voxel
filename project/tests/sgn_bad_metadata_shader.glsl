uni form float gain;

void generate(float x, float bias, out float sdf) {
	sdf = x * gain + bias;
}
