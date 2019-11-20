
#include "FTiXBoundingSphere.h"
#include "TiXExporterBPLibrary.h"
#include "FSmallestEncloseSphere.h"

FSphere FTiXBoundingSphere::GetBoundingSphere(const TArray<FVector>& Points)
{
	FSphere Sphere;
	//TVector<vector3df64> Points64;
	//Points64.resize(Points.size());
	//for (uint32 p = 0 ; p < (uint32)Points.size() ; ++ p)
	//{
	//	Points64[p].X = Points[p].X;
	//	Points64[p].Y = Points[p].Y;
	//	Points64[p].Z = Points[p].Z;
	//}
	FSmallestEncloseSphere<float> SmallSphere(Points);
	Sphere.W = SmallSphere.GetRadius();
	Sphere.Center = SmallSphere.GetCenter();

	return Sphere;
}