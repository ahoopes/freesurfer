from freesurfer.samseg.figures import initVisualizer
import numpy as np
from freesurfer.samseg.io import kvlReadCompressionLookupTable, kvlReadSharedGMMParameters
import os
from freesurfer.samseg.utilities import requireNumpyArray
from . import gemsbindings as gems


def getModelSpecifications(atlasDir, userModelSpecifications={}):

    # Create default model specifications as a dictionary
    FreeSurferLabels, names, colors = kvlReadCompressionLookupTable(os.path.join(atlasDir, 'compressionLookupTable.txt'))
    sharedGMMParameters = kvlReadSharedGMMParameters(os.path.join(atlasDir, 'sharedGMMParameters.txt'))

    modelSpecifications = {
        'FreeSurferLabels': FreeSurferLabels,
        'atlasFileName': os.path.join(atlasDir, 'atlas_level2.txt.gz'),
        'names': names,
        'colors': colors,
        'sharedGMMParameters': sharedGMMParameters,
        'useDiagonalCovarianceMatrices': True,
        'brainMaskingSmoothingSigma': 3.0,  # sqrt of the variance of a Gaussian blurring kernel
        'brainMaskingThreshold': 0.01,
        'K': 0.1,  # stiffness of the mesh
        'biasFieldSmoothingKernelSize': 50,  # distance in mm of sinc function center to first zero crossing
    }

    modelSpecifications.update(userModelSpecifications)

    return modelSpecifications


def getOptimizationOptions(atlasDir, userOptimizationOptions={}):

    # Create default optimization options as a dictionary
    optimizationOptions = {
        'maximumNumberOfDeformationIterations': 20,
        'absoluteCostPerVoxelDecreaseStopCriterion': 1e-4,
        'verbose': False,
        'maximalDeformationStopCriterion': 0.001,  # measured in pixels
        'lineSearchMaximalDeformationIntervalStopCriterion': 0.001,
        'maximalDeformationAppliedStopCriterion': 0.0,
        'BFGSMaximumMemoryLength': 12,
        'multiResolutionSpecification':
            [
                {'atlasFileName': os.path.join(atlasDir, 'atlas_level1.txt.gz'),
                 'targetDownsampledVoxelSpacing': 2.0,
                 'maximumNumberOfIterations': 100,
                 'estimateBiasField': True
                 },
                {'atlasFileName': os.path.join(atlasDir, 'atlas_level2.txt.gz'),
                 'targetDownsampledVoxelSpacing': 1.0,
                 'maximumNumberOfIterations': 100,
                 'estimateBiasField': True
                 }
            ]
    }

    # Over-write with any user specified options. The 'multiResolutionSpecification' key has as value a list
    # of dictionaries which we shouldn't just over-write, but rather update themselves, so this is special case
    userOptimizationOptionsCopy = userOptimizationOptions.copy()
    key = 'multiResolutionSpecification'
    if key in userOptimizationOptionsCopy:
        userList = userOptimizationOptionsCopy[key]
        defaultList = optimizationOptions[key]
        for levelNumber in range(len(defaultList)):
            if levelNumber < len(userList):
                defaultList[levelNumber].update(userList[levelNumber])
            else:
                del defaultList[levelNumber]
        del userOptimizationOptionsCopy[key]
    optimizationOptions.update(userOptimizationOptionsCopy)

    return optimizationOptions


def readCroppedImages(imageFileNames, transformedTemplateFileName):
    # Read the image data from disk. At the same time, construct a 3-D affine transformation (i.e.,
    # translation, rotation, scaling, and skewing) as well - this transformation will later be used
    # to initially transform the location of the atlas mesh's nodes into the coordinate system of the image.
    imageBuffers = []
    for imageFileName in imageFileNames:
        # Get the pointers to image and the corresponding transform
        image = gems.KvlImage(imageFileName, transformedTemplateFileName)
        transform = image.transform_matrix
        cropping = image.crop_slices
        imageBuffers.append(image.getImageBuffer())

    imageBuffers = np.transpose(imageBuffers, axes=[1, 2, 3, 0])

    # Also read in the voxel spacing -- this is needed since we'll be specifying bias field smoothing kernels,
    # downsampling steps etc in mm.
    nonCroppedImage = gems.KvlImage(imageFileNames[0])
    imageToWorldTransformMatrix = nonCroppedImage.transform_matrix.as_numpy_array
    voxelSpacing = np.sum(imageToWorldTransformMatrix[0:3, 0:3] ** 2, axis=0) ** (1 / 2)

    #
    return imageBuffers, transform, voxelSpacing, cropping


def showImage(data):
    range = (data.min(), data.max())

    Nx = data.shape[0]
    Ny = data.shape[1]
    Nz = data.shape[2]

    x = round(Nx / 2)
    y = round(Ny / 2)
    z = round(Nz / 2)

    xySlice = data[:, :, z]
    xzSlice = data[:, y, :]
    yzSlice = data[x, :, :]

    patchedSlices = np.block([[xySlice, xzSlice], [yzSlice.T, np.zeros((Nz, Nz)) + range[0]]])

    import matplotlib.pyplot as plt  # avoid importing matplotlib by default
    plt.imshow(patchedSlices.T, cmap=plt.cm.gray, vmin=range[0], vmax=range[1])
    # plt.gray()
    # plt.imshow( patchedSlices.T, vmin=range[ 0 ], vmax=range[ 1 ] )
    # plt.show()
    plt.axis('off')


def maskOutBackground(imageBuffers, atlasFileName, transform, brainMaskingSmoothingSigma, brainMaskingThreshold,
                      probabilisticAtlas, visualizer=None, maskOutZeroIntensities=True):
    # Setup a null visualizer if necessary
    if visualizer is None:
        visualizer = initVisualizer(False, False)

    # Read the affinely coregistered atlas mesh (in reference position)
    mesh = probabilisticAtlas.getMesh(atlasFileName, transform)

    # Mask away uninteresting voxels. This is done by a poor man's implementation of a dilation operation on
    # a non-background class mask; followed by a cropping to the area covered by the mesh (needed because
    # otherwise there will be voxels in the data with prior probability zero of belonging to any class)
    imageSize = imageBuffers.shape[0:3]
    labelNumber = 0
    backgroundPrior = mesh.rasterize_1a(imageSize, labelNumber)

    # Threshold background prior at 0.5 - this helps for atlases built from imperfect (i.e., automatic)
    # segmentations, whereas background areas don't have zero probability for non-background structures
    backGroundThreshold = 2 ** 8
    backGroundPeak = 2 ** 16 - 1
    backgroundPrior = np.ma.filled(np.ma.masked_greater(backgroundPrior, backGroundThreshold),
                                   backGroundPeak).astype(np.float32)

    visualizer.show(probabilities=backgroundPrior, images=imageBuffers, window_id='samsegment background',
                    title='Background Priors')

    smoothingSigmas = [1.0 * brainMaskingSmoothingSigma] * 3
    smoothedBackgroundPrior = gems.KvlImage.smooth_image_buffer(backgroundPrior, smoothingSigmas)
    visualizer.show(probabilities=smoothedBackgroundPrior, window_id='samsegment smoothed',
                    title='Smoothed Background Priors')

    # 65535 = 2^16 - 1. priors are stored as 16bit ints
    # To put the threshold in perspective: for Gaussian smoothing with a 3D isotropic kernel with variance
    # diag( sigma^2, sigma^2, sigma^2 ) a single binary "on" voxel at distance sigma results in a value of
    # 1/( sqrt(2*pi)*sigma )^3 * exp( -1/2 ).
    # More generally, a single binary "on" voxel at some Eucledian distance d results in a value of
    # 1/( sqrt(2*pi)*sigma )^3 * exp( -1/2*d^2/sigma^2 ). Turning this around, if we threshold this at some
    # value "t", a single binary "on" voxel will cause every voxel within Eucledian distance
    #
    #   d = sqrt( -2*log( t * ( sqrt(2*pi)*sigma )^3 ) * sigma^2 )
    #
    # of it to be included in the mask.
    #
    # As an example, for 1mm isotropic data, the choice of sigma=3 and t=0.01 yields ... complex value ->
    # actually a single "on" voxel will then not make any voxel survive, as the normalizing constant (achieved
    # at Mahalanobis distance zero) is already < 0.01
    brainMaskThreshold = 65535.0 * (1.0 - brainMaskingThreshold)
    brainMask = np.ma.less(smoothedBackgroundPrior, brainMaskThreshold)

    # Crop to area covered by the mesh
    alphas = mesh.alphas
    areaCoveredAlphas = [[0.0, 1.0]] * alphas.shape[0]
    mesh.alphas = areaCoveredAlphas  # temporary replacement of alphas
    areaCoveredByMesh = mesh.rasterize_1b(imageSize, 1)
    mesh.alphas = alphas  # restore alphas
    brainMask = np.logical_and(brainMask, areaCoveredByMesh)

    # If a pixel has a zero intensity in any of the contrasts, that is also masked out across all contrasts
    if maskOutZeroIntensities:
        numberOfContrasts = imageBuffers.shape[-1]
        for contrastNumber in range(numberOfContrasts):
            brainMask *= imageBuffers[:, :, :, contrastNumber] > 0

    # Mask the images
    maskedImageBuffers = imageBuffers.copy()
    maskedImageBuffers[np.logical_not(brainMask), :] = 0

    #
    return maskedImageBuffers, brainMask


def undoLogTransformAndBiasField(imageBuffers, biasFields, mask):
    #
    expBiasFields = np.zeros(biasFields.shape, order='F')
    numberOfContrasts = imageBuffers.shape[-1]
    for contrastNumber in range(numberOfContrasts):
        # We're computing it also outside of the mask, but clip the intensities there to the range
        # observed inside the mask (with some margin) to avoid crazy extrapolation values
        biasField = biasFields[:, :, :, contrastNumber]
        clippingMargin = np.log(2)
        clippingMin = biasField[mask].min() - clippingMargin
        clippingMax = biasField[mask].max() + clippingMargin
        biasField[biasField < clippingMin] = clippingMin
        biasField[biasField > clippingMax] = clippingMax
        expBiasFields[:, :, :, contrastNumber] = np.exp(biasField)

    #
    expImageBuffers = np.exp(imageBuffers) / expBiasFields

    #
    return expImageBuffers, expBiasFields


def writeImage(fileName, buffer, cropping, example):

    # Write un-cropped image to file
    uncroppedBuffer = np.zeros(example.getImageBuffer().shape, dtype=np.float32, order='F')
    uncroppedBuffer[cropping] = buffer
    gems.KvlImage(requireNumpyArray(uncroppedBuffer)).write(fileName, example.transform_matrix)


def logTransform(imageBuffers, mask):

    logImageBuffers = imageBuffers.copy()
    logImageBuffers[np.logical_not(mask), :] = 1
    logImageBuffers = np.log(logImageBuffers)

    #
    return logImageBuffers


def scaleBiasFields(biasFields, imageBuffers, mask, posteriors, targetIntensity=None, targetSearchStrings=None,
                        names=None):

    # Subtract a constant from the bias fields such that after bias field correction and exp-transform, the
    # average intensiy in the target structures will be targetIntensity
    if targetIntensity is not None:
        data = imageBuffers[mask, :] - biasFields[mask, :]
        targetWeights = np.zeros(data.shape[0])
        for searchString in targetSearchStrings:
            for structureNumber, name in enumerate(names):
                if searchString in name:
                    targetWeights += posteriors[:, structureNumber]
        offsets = np.log(targetIntensity) - np.log(np.exp(data).T @ targetWeights / np.sum(targetWeights))
        biasFields -= offsets.reshape([1, 1, 1, biasFields.shape[-1]])

        #
        scalingFactors = np.exp(offsets)
    else:
        scalingFactors = np.ones(imageBuffers.shape[-1])

    return scalingFactors


def writeResults(imageFileNames, savePath, imageBuffers, mask, biasFields, posteriors, FreeSurferLabels, cropping,
                 targetIntensity=None, targetSearchStrings=None, names=None, threshold=None,
                 thresholdSearchString=None, savePosteriors=False):

    # Convert into a crisp, winner-take-all segmentation, labeled according to the FreeSurfer labeling/naming convention
    if threshold is not None:
        # Figure out the structure number of the special snowflake structure
        for structureNumber, name in enumerate(names):
            if thresholdSearchString in name:
                thresholdStructureNumber = structureNumber
                break

        # Threshold
        print('thresholding posterior of ', names[thresholdStructureNumber], 'with threshold:', threshold)
        tmp = posteriors[:, thresholdStructureNumber].copy()
        posteriors[:, thresholdStructureNumber] = posteriors[:, thresholdStructureNumber] > threshold

        # Majority voting
        structureNumbers = np.array(np.argmax(posteriors, 1), dtype=np.uint32)

        # Undo thresholding in posteriors
        posteriors[:, thresholdStructureNumber] = tmp

    else:
        # Majority voting
        structureNumbers = np.array(np.argmax(posteriors, 1), dtype=np.uint32)

    freeSurferSegmentation = np.zeros(imageBuffers.shape[0:3], dtype=np.uint16)
    FreeSurferLabels = np.array(FreeSurferLabels, dtype=np.uint16)
    freeSurferSegmentation[mask] = FreeSurferLabels[structureNumbers]

    #
    scalingFactors = scaleBiasFields(biasFields, imageBuffers, mask, posteriors, targetIntensity,
                                     targetSearchStrings, names)

    # Get corrected intensities and bias field images in the non-log transformed domain
    expImageBuffers, expBiasFields = undoLogTransformAndBiasField(imageBuffers, biasFields, mask)

    # Write out various images - segmentation first
    exampleImage = gems.KvlImage(imageFileNames[0])
    image_base_path, _ = os.path.splitext(imageFileNames[0])
    _, scanName = os.path.split(image_base_path)
    writeImage(os.path.join(savePath, scanName + '_crispSegmentation.nii'), freeSurferSegmentation, cropping,
              exampleImage)
    for contrastNumber, imageFileName in enumerate(imageFileNames):
        image_base_path, _ = os.path.splitext(imageFileName)
        _, scanName = os.path.split(image_base_path)

        # Bias field
        writeImage(os.path.join(savePath, scanName + '_biasField.nii'), expBiasFields[..., contrastNumber],
                   cropping, exampleImage)

        # Bias field corrected image
        writeImage(os.path.join(savePath, scanName + '_biasCorrected.nii'), expImageBuffers[..., contrastNumber],
                   cropping, exampleImage)

        # Save a note indicating the scaling factor
        with open(os.path.join(savePath, scanName + '_scaling-factor.txt'), 'w') as f:
            print(scalingFactors[contrastNumber], file=f)

    if savePosteriors:
        posteriorPath = os.path.join(savePath, 'posteriors')
        os.makedirs(posteriorPath, exist_ok=True)
        for i, name in enumerate(names):
            pvol = np.zeros(imageBuffers.shape[:3], dtype=np.float32)
            pvol[mask] = posteriors[:, i]
            writeImage(os.path.join(posteriorPath, name + '.nii'), pvol, cropping, exampleImage)

    # Compute volumes in mm^3
    volumeOfOneVoxel = np.abs(np.linalg.det(exampleImage.transform_matrix.as_numpy_array[0:3, 0:3]))
    volumesInCubicMm = (np.sum(posteriors, axis=0)) * volumeOfOneVoxel

    return volumesInCubicMm
