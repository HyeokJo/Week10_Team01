#pragma once
#include <cmath>
#include "Vector.h"
#include "Vector2.h"
#include "Vector4.h"
//#include "TVector.h"
#include "Matrix.h"
#include "Quat.h"
#include "Rotator.h"
#include "Collision/Ray.h"
#include "VectorRegister.h"
#include "MatrixRegister.h"

#define PI 3.141592f
#define INDEX_NONE -1

namespace FMath
{
	//static float PI = std::acosf(-1);

	template <typename T>
	static inline T Min(T A, T B)
	{
		return (A >= B ? B : A);
	}

	template <typename T>
	static inline T Max(T A, T B)
	{
		return (A >= B ? A: B);
	}

	template <typename T>
	static inline T Clamp(T InValue, T InMin, T InMax)
	{
		return Max(InMin, Min(InValue, InMax));
	}

	static inline bool IsNearlyZero(float Value, float ErrorTolerance = 1e-20f)
	{
		return std::abs(Value) <= ErrorTolerance;
	}

	static inline bool IsNearlyEqual(float Value1, float Value2, float ErrorTolerance = 1e-4f)
	{
		return std::abs(Value1 - Value2) <= ErrorTolerance;
	}

	static inline float RadiansToDegrees(float Radian)
	{
		return Radian * (180.0f / PI);
	}

	static inline float DegreesToRadians(float Degree)
	{
		return Degree * (PI / 180.0f);
	}

	template <typename T>
	static inline T Lerp(const T& A, const T& B, float Ratio)
	{
		return A * (1.f - Ratio) + B * Ratio;
	}

	static float FInterpTo(float Current, float Target, float DeltaTime, float InterpSpeed)
	{
		if (InterpSpeed <= 0.0f)
		{
			return Target;
		}
		return Current + (Target - Current) * Clamp(DeltaTime * InterpSpeed, 0.0f, 1.0f);
	}

	static FVector VInterpolate(const FVector& Current, const FVector& Target, float DeltaTime, float InterpSpeed)
	{
		if (InterpSpeed <= 0.0f)
		{
			return Target;
		}

		return Current + (Target - Current) * Clamp(DeltaTime * InterpSpeed, 0.0f, 1.0f);
	}

	static FQuat QInterpTo(const FQuat& Current, const FQuat& Target, float DeltaTime, float InterpSpeed)
	{
		if (InterpSpeed <= 0.0f)
		{
			return Target;
		}

		return FQuat::Slerp(Current, Target, Clamp(DeltaTime * InterpSpeed, 0.0f, 1.0f));
	}

	static float ExpSmoothingAlpha(float DeltaTime, float Speed)
	{
		return Speed <= 0.0f ? 1.0f : 1.0f - std::exp(-Speed * DeltaTime);
	}

	static FVector VExpSmoothTo(const FVector& Current, const FVector& Target, float DeltaTime, float Speed)
	{
		return Current + (Target - Current) * ExpSmoothingAlpha(DeltaTime, Speed);
	}

	static FQuat QExpSmoothTo(const FQuat& Current, const FQuat& Target, float DeltaTime, float Speed)
	{
		return FQuat::Slerp(Current, Target, ExpSmoothingAlpha(DeltaTime, Speed));
	}
}