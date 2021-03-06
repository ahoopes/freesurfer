function [alpha, rho, acffit] = fast_ar1w_fit(acf,maxlag)
% [alpha rho acffit] = fmri_ar1w_fit(acf,<maxlag>)
%
% Fits the autocorrelation function acf to the model:
%    acf(n) = 1,                n = 0
%    acf(n) = (1-alpha)*rho^n,  n != 0
% and returns alpha and rho.  
%
% If acffit is requested, the acf generated by alpha and rho is
% returned.
%
% acf can have any number of columns
% maxlag only the first maxlag components of acf are fit.
%
% The acf undergoes a substantial amount of preprocessing prior to the
% fit. It is prepared by taking the abs and making it monotonic. The
% rho values for those columns whose original acf is negative is set
% to be negative. The actual fit is done on the log10(acf).
%
% BUGS: 
%
% 1. It's possible that alpha can be a large negative number (<-1)
% when rho is very small. It's not clear that this is really a
% problem, but it does make the alpha/rho parameters difficult to
% interpret. Should force alpha=0 if rho is "small".
%
% 2. It's possible that abs(rho) = 1 because of the monotonic
% function can make two successive lags equal (requiring rho = 1).
% Should enforce rho not to exceed some thresh (eg, .8) or apply
% a taper prior to fitting.
%
% See also: fast_ar1w_acf
% 
%


%
% fast_ar1w_fit.m
%
% Original Author: Doug Greve
%
% Copyright © 2011 The General Hospital Corporation (Boston, MA) "MGH"
%
% Terms and conditions for use, reproduction, distribution and contribution
% are found in the 'FreeSurfer Software License Agreement' contained
% in the file 'LICENSE' found in the FreeSurfer distribution, and here:
%
% https://surfer.nmr.mgh.harvard.edu/fswiki/FreeSurferSoftwareLicense
%
% Reporting: freesurfer@nmr.mgh.harvard.edu
%

alpha = [];
rho = [];

if(nargin ~= 1 & nargin ~= 2)
  msg = 'USAGE [alpha rho acffit] = fmri_ar1w_fit(acf,maxlag)';
  qoe(msg); error(msg);
end

[nacf nv] = size(acf); % number of frames in acf, num voxels
if(exist('maxlag') ~= 1) maxlag = nacf; end

%alpha = zeros(1,nv);
%rho   = zeros(1,nv);

% Remove zero lag and everything beyond maxlag %
acf = acf(2:maxlag,:);

% For neg, assign neg to rho if the lag1 component is < 0
indneg = find(acf(1,:) < 0);

% Make the abs monotonically decreasing
acf = fast_monotonize(abs(acf));

X = [ones(nacf-1,1) [1:nacf-1]'];
beta = (inv(X'*X)*X')*log10(acf);
%[beta, rvar] = fast_glmfit(log10(acf),X);
%[F, Fsig] = fast_fratio(beta,X,rvar,eye(2));

alpha = 1 - 10.^beta(1,:);
rho = 10.^beta(2,:);

rho(indneg) = -rho(indneg);

% Don't let it go unstable
%indrhog1 = find(abs(rho)>1);
%rho(indrhog1) = .9*sign(rho(indrhog1));

if(nargout == 3)
  acffit = fast_ar1w_acf(alpha,rho,nacf);
end

return;






