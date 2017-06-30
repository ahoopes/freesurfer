function run_samseg(imageFileName1,outDir,nThreadsStr,UseGPUStr,imageFileName2,imageFileName3,imageFileName4,imageFileName5,imageFileName6,verboseStr)
% This function is a wrapper for running samsegment. This wrapper can be compiled
% (meaning that all the inputs are strings).
%
% TODO: write header
%

fprintf('Matlab version %s\n',version);


% =================================================================
%
% Process input arguments
%
% =================================================================

%subject with different contrasts you can feed those in as well. Make sure though that the scans are coregistered
%imageFileNames{2} = '...'; %If you have more scans of the same
imageFileNames = cell(0,0);
imageFileNames{1} =  imageFileName1;

if(~strcmp(imageFileName2,'')), imageFileNames{2} =  imageFileName2; end
if(~strcmp(imageFileName3,'')), imageFileNames{3} =  imageFileName3; end
if(~strcmp(imageFileName4,'')), imageFileNames{4} =  imageFileName4; end
if(~strcmp(imageFileName5,'')), imageFileNames{5} =  imageFileName5; end
if(~strcmp(imageFileName6,'')), imageFileNames{6} =  imageFileName6; end

nThreads = sscanf(nThreadsStr,'%d');
verbose = sscanf(verboseStr, '%d');

if verbose
    fprintf('Matlab version %s\n',version);
    fprintf('input file1 %s\n',imageFileName1);
    fprintf('input file2 %s\n',imageFileName2);
    fprintf('input file3 %s\n',imageFileName3);
    fprintf('input file4 %s\n',imageFileName4);
    fprintf('input file5 %s\n',imageFileName5);
    fprintf('input file6 %s\n',imageFileName6);
    fprintf('output path %s\n',outDir);
    fprintf('nThreads = %s\n',nThreadsStr);
    fprintf('useGPU = %s (currently disabled) \n',UseGPUStr);
    fprintf('verbose = %s\n',verboseStr);
end

% =================================================================
%
% Setup from env + hardcoded parameters
%
% =================================================================

% set SAMSEG_DATA_DIR as an environment variable, eg,
%  setenv SAMSEG_DATA_DIR /autofs/cluster/koen/koen/GEMSapplications/wholeBrain
AvgDataDir = getenv('SAMSEG_DATA_DIR');

% Set this to true if you want to see some figures during the run.
showFigures = false; 

% =================================================================
%
% Start processing
%
% =================================================================

samsegStartTime = tic;

fprintf('entering kvlClear\n');
kvlClear; % Clear all the wrapped C++ stuff

fprintf('entering registerAtlas\n');


% =================================================================
%
% Register atlas to image(s) using a boiled-down samseg model
% thereby obtaining an affine(ish) transformation
%
% =================================================================


[worldToWorldTransformMatrix] = registerAtlas(...
    imageFileNames{1}, ...
    outDir, ...
    AvgDataDir, ...
    verbose, ...
    showFigures);


% =============================================================================================
%
% For historical reasons the samsegment function figures out the affine transformation from
% a transformed MNI template (where before transformation this template defines the segmentation
% mesh atlas domain). This is a bit silly really, but for now let's just play along and make
% sure we generate it
%
% =============================================================================================
templateFileName = sprintf('%s/mni305_masked_autoCropped.mgz', AvgDataDir);

[ origTemplate, origTemplateTransform ] = kvlReadImage( templateFileName );

transformedTemplateFileName = sprintf('%s/mni305_masked_autoCropped_coregistered.mgz', outDir);

kvlWriteImage( origTemplate, transformedTemplateFileName, ...
    kvlCreateTransform( double( worldToWorldTransformMatrix * kvlGetTransformMatrix( origTemplateTransform ) ) ) );

% =================================================================
%
% Do the segmentation magic
%
% =================================================================


fprintf('entering samsegment \n');

multiResolutionSpecification = struct( [] );
multiResolutionSpecification{ 1 }.meshSmoothingSigma = 2.0; % In mm
multiResolutionSpecification{ 1 }.targetDownsampledVoxelSpacing = 2.0; % In mm
multiResolutionSpecification{ 1 }.maximumNumberOfIterations = 100;
multiResolutionSpecification{ 2 }.meshSmoothingSigma = 0.0; % In mm
multiResolutionSpecification{ 2 }.targetDownsampledVoxelSpacing = 1.0; % In mm
multiResolutionSpecification{ 2 }.maximumNumberOfIterations = 100;

samsegment(...
    imageFileNames, ...
    outDir, ...
    AvgDataDir, ...
    'multiResolutionSpecification', multiResolutionSpecification, ...
    'maximumNumberOfDeformationIterations', 20, ...
    'absoluteCostPerVoxelDecreaseStopCriterion', 1e-4, ...
    'maximalDeformationStopCriterion', 1e-3, ...  % Measured in pixels
    'lineSearchMaximalDeformationIntervalStopCriterion', 1e-3, ... % idem
    'maximalDeformationAppliedStopCriterion', 0.0, ...
    'BFGSMaximumMemoryLength', 12, ...
    'K', 0.1, ... % Stiffness of the mesh
    'brainMaskingSmoothingSigma', 3, ... % sqrt of the variance of a Gaussian blurring kernel
    'brainMaskingThreshold', 0.01, ... % mask out voxels with less than % probability of being brain
    'verbose', verbose, ...
    'showFigures', showFigures, ...
    'nThreads', nThreads);

fprintf('#@# samseg done %6.4f min\n',toc( samsegStartTime )/60);

