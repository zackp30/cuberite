
float IntNoise2D(int a_Seed, int a_X, int a_Y)
{
	int n = a_X + a_Y * 57 + a_Seed * 57 * 57;
	n = (n << 13) ^ n;
	return (1 - (float)((n * (n * n * 15731 + 789221) + 1376312589) & 0x7fffffff) / 1073741824);
	// returns a float number in the range of [-1, 1]
}

float CubicInterpolate(float a_A, float a_B, float a_C, float a_D, float a_Pct)
{
	float P = (a_D - a_C) - (a_A - a_B);
	float Q = (a_A - a_B) - P;
	float R = a_C - a_A;
	float S = a_B;

	return ((P * a_Pct + Q) * a_Pct + R) * a_Pct + S;
}


float CubicNoise2D(int a_Seed, float a_X, float a_Y)
{
	const int	BaseX = floor(a_X);
	const int	BaseY = floor(a_Y);
	
	const float points[4][4] =
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
			IntNoise2D(a_Seed, BaseX - 1, BaseY + 2), 
			IntNoise2D(a_Seed, BaseX, BaseY + 2), 
			IntNoise2D(a_Seed, BaseX + 1, BaseY + 2), 
			IntNoise2D(a_Seed, BaseX + 2, BaseY + 2), 
		},
	};

	const float FracX = a_X - BaseX;
	const float interp1 = CubicInterpolate(points[0][0], points[0][1], points[0][2], points[0][3], FracX);
	const float interp2 = CubicInterpolate(points[1][0], points[1][1], points[1][2], points[1][3], FracX);
	const float interp3 = CubicInterpolate(points[2][0], points[2][1], points[2][2], points[2][3], FracX);
	const float interp4 = CubicInterpolate(points[3][0], points[3][1], points[3][2], points[3][3], FracX);


	const float FracY = a_Y - BaseY;
	return CubicInterpolate(interp1, interp2, interp3, interp4, FracY);
}

typedef struct
{
	float m_HeightFreq1;
	float m_HeightFreq2;
	float m_HeightFreq3;
	
	float m_HeightAmp1;
	float m_HeightAmp2;
	float m_HeightAmp3;
} HeiGenState;

float GetNoise(int a_Seed, HeiGenState a_State, float x, float y)
{
	float oct1 = CubicNoise2D(a_Seed, x * a_State.m_HeightFreq1, y * a_State.m_HeightFreq1) * a_State.m_HeightAmp1;
	float oct2 = CubicNoise2D(a_Seed, x * a_State.m_HeightFreq2, y * a_State.m_HeightFreq2) * a_State.m_HeightAmp2;
	float oct3 = CubicNoise2D(a_Seed, x * a_State.m_HeightFreq3, y * a_State.m_HeightFreq3) * a_State.m_HeightAmp3;

	float height = CubicNoise2D(a_Seed, x * 0.1f, y * 0.1f ) * 2;

	float flatness = ((CubicNoise2D(a_Seed, x * 0.5f, y * 0.5f) + 1.f) * 0.5f) * 1.1f; // 0 ... 1.5
	flatness *= flatness * flatness;

	return (oct1 + oct2 + oct3) * flatness + height;
}


__kernel void GenHeightMap(int a_Seed, HeiGenState a_State, int a_ChunkX, int a_ChunkZ, __global unsigned char * a_HeightMap)
{
	int z = get_global_id(0);
	int x = get_global_id(1);
	const float zz = (float)(a_ChunkZ * 16 + z);
	const float xx = (float)(a_ChunkX * 16 + x);
	
	int hei = 64 + (int)(GetNoise(a_Seed, a_State, xx * 0.05f, zz * 0.05f) * 16);
	if (hei < 10)
	{
		hei = 10;
	}
	if (hei > 250)
	{
		hei = 250;
	}
	a_HeightMap[x + 16 * z] = hei;
}
