#include <iostream>
#include <vector>

#include "geodesicsheat.h"

extern "C"
{
#include "macros.h"
#include "mrisurf.h"
}


// --------------------------------------------------------
//               fast, 3D vector arithmetic
// --------------------------------------------------------


typedef float Vec3[3];


static void vec3Sub(Vec3 w, const Vec3 u, const Vec3 v)
{
  w[0] = u[0] - v[0];
  w[1] = u[1] - v[1];
  w[2] = u[2] - v[2];
}


static void vec3Cross(Vec3 w, const Vec3 u, const Vec3 v)
{
  w[0] = u[1]*v[2] - u[2]*v[1];
  w[1] = u[2]*v[0] - u[0]*v[2];
  w[2] = u[0]*v[1] - u[1]*v[0];
}


static float vec3Dot(const Vec3 u, const Vec3 v)
{
  return u[0]*v[0] + u[1]*v[1] + u[2]*v[2];
}


static float vec3Norm(const Vec3 u)
{
  return sqrt(u[0]*u[0] + u[1]*u[1] + u[2]*u[2]);
}


static void vec3Scale(Vec3 u, float a)
{
  u[0] *= a;
  u[1] *= a;
  u[2] *= a;
}


// --------------------------------------------------------
//                        geodesics 
// --------------------------------------------------------


struct SparseMatrix {
  int nRows, nCols, nNonZeros;
  std::vector<float> values;
  std::vector<int> rowIndices, colStart;
};


struct DenseMatrix {
  int nRows, nCols;
  std::vector<float> values;
};


struct HeatMesh {
  std::vector<float> vertices, vertexAreas;
  std::vector<float> weights, edgeNormals, weightedEdges;
  SparseMatrix laplacian;
};


static void buildMatrices(MRIS* surf, HeatMesh* mesh);
static void computeMeshProperties(MRIS* surf, HeatMesh* mesh);


extern "C"
void GEOprecompute(MRIS* surf)
{
  HeatMesh mesh;
  computeMeshProperties(surf, &mesh);
}


void computeMeshProperties(MRIS* surf, HeatMesh* mesh)
{
  // init mesh vertices:
  mesh->vertices.clear();
  mesh->vertices.resize(3*surf->nvertices);
  float *vtx = &mesh->vertices[0];
  for (int vn = 0 ; vn < surf->nvertices ; vn++, vtx+=3)
  { 
    vtx[0] = surf->vertices[vn].x;
    vtx[1] = surf->vertices[vn].y;
    vtx[2] = surf->vertices[vn].z;
  }

  // init mesh weights:
  mesh->weights.clear();
  mesh->weights.resize(3*surf->nfaces);
  float *w = &mesh->weights[0];

  // init edge normals:
  mesh->edgeNormals.clear();
  mesh->edgeNormals.resize(9*surf->nfaces);
  float *t0 = &mesh->edgeNormals[0];
  float *t1 = &mesh->edgeNormals[3];
  float *t2 = &mesh->edgeNormals[6];

  // init weighted edges:
  mesh->weightedEdges.clear();
  mesh->weightedEdges.resize(9*surf->nfaces);
  float *e0 = &mesh->weightedEdges[0];
  float *e1 = &mesh->weightedEdges[3];
  float *e2 = &mesh->weightedEdges[6];

  // init vertex areas:
  mesh->vertexAreas.clear();
  mesh->vertexAreas.resize(surf->nvertices);

  FACE *f;
  float *p[3];
  float uvCosTheta, uvSinTheta, area;
  Vec3 u, v, N;
  int j0, j1, j2;

  // iterate over faces:
  for (int fn = 0 ; fn < surf->nfaces ; fn++)
  {
    f = &surf->faces[fn];
    
    // vertex coordinates
    p[0] = &mesh->vertices[f->v[0]*3];
    p[1] = &mesh->vertices[f->v[1]*3];
    p[2] = &mesh->vertices[f->v[2]*3];

    // ============== compute weights ==============

    // iterate over triangle corners:
    for (int k = 0 ; k < 3 ; k++)
    {
      // get outgoing edge vectors u, v at current corner:
      j0 = (0+k) % 3;
      j1 = (1+k) % 3;
      j2 = (2+k) % 3;
      vec3Sub(u, p[j1], p[j0]);
      vec3Sub(v, p[j2], p[j0]);

      vec3Cross(N, u, v);
      uvSinTheta = vec3Norm(N);
      uvCosTheta = vec3Dot(u, v);
      w[k] = 0.5 * uvCosTheta / uvSinTheta;
    }

    // ================= get edges =================

    // compute edge vectors:
    vec3Sub(e0, p[2], p[1]);
    vec3Sub(e1, p[0], p[2]);
    vec3Sub(e2, p[1], p[0]);

    // compute triangle normal:
    vec3Cross(N, e0, e1);

    // compute rotated edge vectors:
    vec3Cross(t0, N, e0);
    vec3Cross(t1, N, e1);
    vec3Cross(t2, N, e2);

    // scale edge vectors by cotangent weights at opposing corners:
    vec3Scale(e0, w[0]);
    vec3Scale(e1, w[1]);
    vec3Scale(e2, w[2]);

    // ============ compute vertex areas ===========

    // compute (one-third of) the triangle area = |u x v|:
    vec3Sub(u, p[1], p[0]);
    vec3Sub(v, p[2], p[0]);
    vec3Cross(N, u, v);
    area = vec3Norm(N) / 6.0;

    // add contribution to each of the three corner vertices:
    mesh->vertexAreas[f->v[0]] += area;
    mesh->vertexAreas[f->v[1]] += area;
    mesh->vertexAreas[f->v[2]] += area;

    // move to next face:
    w += 3;
    t0 += 9; t1 += 9; t2 += 9;
    e0 += 9; e1 += 9; e2 += 9;
  }
}


void buildMatrices(MRIS* surf, HeatMesh* mesh)
{
  VERTEX *v;
  SparseMatrix *laplacian = &mesh->laplacian;

  // count number of non-zeros (bottom triangle) in laplacian:
  int nNonZeros = 0;
  laplacian->colStart.clear();
  laplacian->colStart.push_back(nNonZeros);
  for (int i = 0 ; i < surf->nvertices ; i++)
  {
    v = &surf->vertices[i];
    nNonZeros++;  // diagonal entry
    for (int nv = 0 ; nv < v->vnum ; nv++)
      if (v->v[nv] > i) nNonZeros++;
    laplacian->colStart.push_back(nNonZeros);
  }

  float columnSum;
  bool diagonalSet;

  // fill non-zero entries:
  int nz = 0;
  for (int i = 0 ; i < surf->nvertices ; i++)
  {
    v = &surf->vertices[i];

    // get sum of neighbor weights
    columnSum = 0.0;
    for (int nv = 0 ; nv < v->vnum ; nv++) columnSum += weight;

    diagonalSet = false;
    for (int nv = 0 ; nv <= v->vnum ; nv++)
    {
      if (!diagonalSet && (nv == v->vnum || v->v[nv] > i))
      {
        
        diagonalSet = true;
        nz++;
      }
    }
  }
}
