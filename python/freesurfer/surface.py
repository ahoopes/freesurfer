import os
import copy
import numpy as np

from . import bindings, warning
from .transform import Transformable, LinearTransform


class Surface(Transformable):
    '''Triangular mesh topology represented by arrays of vertices and faces.'''

    def __init__(self, vertices, faces=None, hemi=None, geom=None):

        # TODEP - this is a temporary fix to support the previous way of loading from a file - it
        # is not an ideal way of handling things and should be removed as soon as possible
        if isinstance(vertices, str):
            warning('moving foward, please load surfaces via fs.Surface.read(filename)')
            result = Surface.read(vertices)
            vertices = result.vertices
            faces = result.faces
            hemi = result.hemi
            geom = result.geom

        self.vertices = vertices
        self.faces = faces
        self.vertex_normals = None
        self.face_normals = None
        self.hemi = hemi
        self.geom = geom

        # compute face and vertex normals
        self.compute_normals()

    def __eq__(self, other):
        equal = np.array_equal(self.vertices, other.vertices) and \
                np.array_equal(self.faces, other.faces) and \
                self.hemi == other.hemi and \
                self.geom == other.geom
        return equal

    # ---- utility ----

    @classmethod
    def read(cls, filename):
        '''Loads surface from file.'''
        if not os.path.isfile(filename):
            raise ValueError('surface file %s does not exist' % filename)
        return bindings.surf.read(os.path.abspath(filename))

    def write(self, filename):
        '''Writes surface to file.'''
        bindings.surf.write(self, os.path.abspath(filename))

    def copy(self):
        '''Returns a deep copy of the surface.'''
        return copy.deepcopy(self)

    # ---- mesh topology ----

    @property
    def nvertices(self):
        '''Total number of vertices in the mesh.'''
        return len(self.vertices)

    @property
    def nfaces(self):
        '''Total number of faces in the mesh.'''
        return len(self.faces)

    def compute_normals(self):
        '''Compute and cache face and vertex normals.'''
        bindings.surf.compute_normals(self)

    def neighboring_faces(self, vertex):
        '''List of face indices that neighbor a vertex.'''
        return np.where(self.faces == vertex)[0]

    # ---- geometry ----

    def bbox(self):
        '''Returns the (min, max) coordinates that define the surface's bounding box.'''
        return self.vertices.min(axis=0), self.vertices.max(axis=0)

    def geometry(self):
        '''Returns the geometry associated with the source volume.
        Note: This is the same as the `geom` member, but is necessary for
        subclassing `Transformable`.'''
        return self.geom

    # ---- parameterization ----

    def parameterize(self, overlay):
        '''Parameterizes an nvertices-length array to a 256 x 512 image. Parameterization method is barycentric.'''
        data = Overlay.ensure(overlay).data
        if len(data) != self.nvertices:
            raise ValueError('overlay length (%d) differs from vertex count (%d)' % (len(data), self.nvertices))
        return bindings.surf.parameterize(self, data).squeeze()

    def sample_parameterization(self, image):
        '''Samples a parameterized image into an nvertices-length array. Sampling method is barycentric.'''
        data = Image.ensure(overlay).data
        return bindings.surf.sample_parameterization(self, data)

    # ---- deprecations ----

    def parameterization_map(self):  # TODEP
        '''Deprecated! '''
        raise DeprecationWarning('parameterization_map() has been removed! Email andrew!') 

    def copy_geometry(self, vol):  # TODEP
        '''Deprecated! '''
        warning('copy_geometry(vol) has been removed! Just use: surf.geom = vol.geometry()')
        self.geom = vol.geometry()

    def isSelfIntersecting(self):  # TODEP
        '''Deprecated!'''
        raise DeprecationWarning('isSelfIntersecting has been removed! Email andrew if you get this!!')

    def fillInterior(self):  # TODEP
        '''Deprecated!'''
        raise DeprecationWarning('fillInterior has been removed! Email andrew if you get this!!')

    def get_vertex_positions(self):  # TODEP
        '''Deprecated - use Surface.vertices directly'''
        return self.vertices

    def set_vertex_positions(self, vertices):  # TODEP
        '''Deprecated - use Surface.vertices directly'''
        self.vertices = vertices

    def get_face_vertices(self):  # TODEP
        '''Deprecated - use Surface.faces directly'''
        warning('"Surface.get_face_vertices()" is deprecated - use "Surface.faces" directly. Sorry for the back and forth...')
        return self.faces

    def get_vertex_normals(self):  # TODEP
        '''Deprecated - use Surface.vertex_normals directly'''
        warning('"Surface.get_vertex_normals()"" is deprecated - use "Surface.vertex_normals" directly. Sorry for the back and forth...')
        return self.vertex_normals

    def get_vertex_faces(self):  # TODEP
        '''Deprecated - use Surface.neighboring_faces instead'''
        raise DeprecationWarning('get_vertex_faces has been removed! Use Surface.neighboring_faces or email andrew if you get this!!!!')

    def vox2surf(self, vol):  # TODEP
        '''Deprecated - vol is no longer needed'''
        return self.vox2surf()

    def surf2vox(self, vol):  # TODEP
        '''Deprecated - vol is no longer needed'''
        return self.surf2vox()
