#include <iostream>
#include <vector>
#include <algorithm>

#include "geodesicsheat.h"

extern "C"
{
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


struct Neighbor {
  int n;
  float x;
  Neighbor(int a, float b) : n(a), x(b) {}
  bool operator < (const Neighbor &nbr) const {return (n < nbr.n);}
};


struct HeatMesh {
  int nvertices;
  std::vector<float> vertices, vertexAreas;
  std::vector<float> weights, edgeNormals, weightedEdges;
  std::vector<bool> onBoundary;
  std::vector<std::vector<Neighbor> > uniqueNeighbors;
};


static void buildMatrices(HeatMesh* mesh, SparseMatrix* laplacian);
static void computeMeshProperties(MRIS* surf, HeatMesh* mesh);


extern "C"
void GEOprecompute(MRIS* surf)
{
  HeatMesh mesh;
  SparseMatrix laplacian;
  computeMeshProperties(surf, &mesh);
  buildMatrices(&mesh, &laplacian);
}


void computeMeshProperties(MRIS* surf, HeatMesh* mesh)
{
  // init mesh vertices:
  mesh->nvertices = surf->nvertices;
  mesh->vertices.clear();
  mesh->vertices.resize(3*mesh->nvertices);
  float *vtx = &mesh->vertices[0];
  for (int vn = 0 ; vn < mesh->nvertices ; vn++, vtx+=3)
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
  mesh->vertexAreas.resize(mesh->nvertices);

  // init neighbors:
  std::vector<std::vector<Neighbor> > neighbors(mesh->nvertices);
  mesh->uniqueNeighbors.clear();
  mesh->uniqueNeighbors.resize(mesh->nvertices);

  float *p[3];
  float uvCosTheta, uvSinTheta, area;
  int j0, j1, j2;
  Vec3 u, v, N;
  FACE *f;

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

    // =============== find neighbors ==============

    // iterate over triangle corners:
    for (int k = 0 ; k < 3 ; k++)
    {
      j0 = (0+k) % 3;
      j1 = (1+k) % 3;
      j2 = (2+k) % 3;
      neighbors[f->v[j0]].push_back(Neighbor(f->v[j1], w[j2]));
      neighbors[f->v[j0]].push_back(Neighbor(f->v[j2], w[j1]));
    }

    // move to next face:
    w += 3;
    t0 += 9; t1 += 9; t2 += 9;
    e0 += 9; e1 += 9; e2 += 9;
  }

  // ============= get unique neighbors ============

  int lastNeighborIndex, count;

  // iterate over triangle corners:
  for (int i = 0 ; i < mesh->nvertices ; i++)
  {
    // sort by index:
    std::sort(neighbors[i].begin(), neighbors[i].end());

    mesh->onBoundary[i] = false;
    lastNeighborIndex = -1;
    count = 0;

    // extract unique elements from neighbor list, summing weights:
    for (unsigned int j = 0 ; j < neighbors[i].size() ; j++)
    {
      // if neighbor is new, add it to unique list:
      if (neighbors[i][j].n != lastNeighborIndex)
      {
        if (count == 1) mesh->onBoundary[i] = true;
        count = 1;
        mesh->uniqueNeighbors[i].push_back(neighbors[i][j]);
        lastNeighborIndex = neighbors[i][j].n;
      }
      else
      {
        mesh->uniqueNeighbors[i].back().x += neighbors[i][j].x;
        count++;
      }
    }

    // if the neighbor was represented once, it must be a boundary vertex:
    if (count == 1) mesh->onBoundary[i] = true;
  }
}


void buildMatrices(HeatMesh* mesh, SparseMatrix* laplacian)
{
  // count number of non-zeros (bottom triangle) in laplacian:
  int nNonZeros = 0;
  std::vector<Neighbor> *nbrs;
  laplacian->colStart.clear();
  laplacian->colStart.push_back(nNonZeros);
  for (int i = 0 ; i < mesh->nvertices ; i++)
  {
    nbrs = &mesh->uniqueNeighbors[i];
    nNonZeros++;  // diagonal entry
    for (unsigned int j = 0 ; j < nbrs->size() ; j++)
      if (nbrs->at(j).n > i) nNonZeros++;
    laplacian->colStart.push_back(nNonZeros);
  }

  // init laplacian:
  laplacian->nRows = mesh->nvertices;
  laplacian->nCols = mesh->nvertices;
  laplacian->values.clear();
  laplacian->values.resize(nNonZeros);
  laplacian->rowIndices.clear();
  laplacian->rowIndices.resize(nNonZeros);

  float columnSum, area;
  bool diagonalSet;
  float hmRegularization = 0;  // todo: change this

  // fill non-zero entries:
  int nz = 0;
  for (int i = 0 ; i < mesh->nvertices ; i++)
  {
    nbrs = &mesh->uniqueNeighbors[i];

    // get sum of neighbor weights:
    columnSum = 0.0;
    for (unsigned int j = 0 ; j < nbrs->size() ; j++)
      columnSum += nbrs->at(j).x;

    diagonalSet = false;
    for (unsigned int j = 0 ; j <= nbrs->size() ; j++)
    {
      if (!diagonalSet && (j == nbrs->size() || nbrs->at(j).n > i))
      {
        laplacian->values[nz] = columnSum + hmRegularization;
        laplacian->rowIndices[nz] = i;
        area = mesh->vertexAreas[i];
        diagonalSet = true;
        nz++;
      }
      if (i < (int)nbrs->size() && nbrs->at(j).n > i)
      {
        laplacian->values[nz] = -nbrs->at(j).x;
        laplacian->rowIndices[nz] = nbrs->at(j).n;
        nz++;
      }
    }
  }
}
