
#define NOISE_DATATYPE float


NOISE_DATATYPE IntNoise2D(int a_Seed, int a_X, int a_Y) const
{
	int n = a_X + a_Y * 57 + Seed * 57 * 57;
	n = (n << 13) ^ n;
	return (1 - (NOISE_DATATYPE)((n * (n * n * 15731 + 789221) + 1376312589) & 0x7fffffff) / 1073741824);
	// returns a float number in the range of [-1, 1]
}

NOISE_DATATYPE CubicInterpolate(NOISE_DATATYPE a_A, NOISE_DATATYPE a_B, NOISE_DATATYPE a_C, NOISE_DATATYPE a_D, NOISE_DATATYPE a_Pct)
{
	NOISE_DATATYPE P = (a_D - a_C) - (a_A - a_B);
	NOISE_DATATYPE Q = (a_A - a_B) - P;
	NOISE_DATATYPE R = a_C - a_A;
	NOISE_DATATYPE S = a_B;

	return ((P * a_Pct + Q) * a_Pct + R) * a_Pct + S;
}


NOISE_DATATYPE CubicNoise2D(int a_Seed, float a_X, NOISE_DATATYPE a_Y)
{
	const int	BaseX = FAST_FLOOR(a_X);
	const int	BaseY = FAST_FLOOR(a_Y);
	
	const NOISE_DATATYPE points[4][4] =
	{
		{ 
			IntNoise2D(a_Seed, BaseX - 1, BaseY - 1),
			IntNoise2D(a_Seed, BaseX, BaseY - 1),
			IntNoise2D(a_Seed, BaseX + 1, BaseY - 1), 
			IntNoise2D(a_Seed, BaseX + 2, BaseY - 1), 
		},
		{ 
			IntNoise2D(a_Seed, BaseX - 1, BaseY),
			IntNoise2D(a_Seed, BaseX, BaseY),
			IntNoise2D(a_Seed, BaseX + 1, BaseY),
			IntNoise2D(a_Seed, BaseX + 2, BaseY),
		},
		{ 
			IntNoise2D(a_Seed, BaseX - 1, BaseY + 1), 
			IntNoise2D(a_Seed, BaseX, BaseY + 1), 
			IntNoise2D(a_Seed, BaseX + 1, BaseY + 1), 
			IntNoise2D(a_Seed, BaseX + 2, BaseY + 1), 
		},
		{ 
			IntNoise2D(BaseX - 1, BaseY + 2), 
			IntNoise2D(BaseX, BaseY + 2), 
			IntNoise2D(BaseX + 1, BaseY + 2), 
			IntNoise2D(BaseX + 2, BaseY + 2), 
		},
	};

	const NOISE_DATATYPE FracX = a_X - BaseX;
	const NOISE_DATATYPE interp1 = CubicInterpolate(points[0][0], points[0][1], points[0][2], points[0][3], FracX);
	const NOISE_DATATYPE interp2 = CubicInterpolate(points[1][0], points[1][1], points[1][2], points[1][3], FracX);
	const NOISE_DATATYPE interp3 = CubicInterpolate(points[2][0], points[2][1], points[2][2], points[2][3], FracX);
	const NOISE_DATATYPE interp4 = CubicInterpolate(points[3][0], points[3][1], points[3][2], points[3][3], FracX);


	const NOISE_DATATYPE FracY = a_Y - BaseY;
	return CubicInterpolate(interp1, interp2, interp3, interp4, FracY);
}

