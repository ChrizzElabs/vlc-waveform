-----BEGIN PGP SIGNED MESSAGE-----
Hash: SHA512

Format: 3.0 (quilt)
Source: mingw-w64
Binary: mingw-w64-common, mingw-w64-i686-dev, mingw-w64-x86-64-dev, mingw-w64-ucrt64-dev, mingw-w64, mingw-w64-tools
Architecture: any all
Version: 12.0.0-5
Maintainer: Stephen Kitt <skitt@debian.org>
Homepage: http://mingw-w64.sf.net
Standards-Version: 4.7.0
Vcs-Browser: https://salsa.debian.org/mingw-w64-team/mingw-w64
Vcs-Git: https://salsa.debian.org/mingw-w64-team/mingw-w64.git
Testsuite: autopkgtest
Testsuite-Triggers: gcc-mingw-w64-x86-64, slapd, wine, wine64
Build-Depends: debhelper-compat (= 13)
Build-Depends-Indep: gcc-mingw-w64-bootstrap <stage1>, gcc-mingw-w64-i686 <!stage1>, gcc-mingw-w64-ucrt64 <!stage1>, gcc-mingw-w64-x86-64 <!stage1>
Package-List:
 mingw-w64 deb devel optional arch=all
 mingw-w64-common deb devel optional arch=all
 mingw-w64-i686-dev deb devel optional arch=all
 mingw-w64-tools deb devel optional arch=any
 mingw-w64-ucrt64-dev deb devel optional arch=all
 mingw-w64-x86-64-dev deb devel optional arch=all
Checksums-Sha1:
 8a725388147e7aa2b367c35e52e91a92713e1faf 10474283 mingw-w64_12.0.0.orig.tar.bz2
 a04bc8eaa853e8455abcb5bdec4b5da5ca3eaf8b 269 mingw-w64_12.0.0.orig.tar.bz2.asc
 f88e2d7c3b5b972b28b8f8ee0053f2ab0562eba3 162596 mingw-w64_12.0.0-5.debian.tar.xz
Checksums-Sha256:
 cc41898aac4b6e8dd5cffd7331b9d9515b912df4420a3a612b5ea2955bbeed2f 10474283 mingw-w64_12.0.0.orig.tar.bz2
 f9b696450b5ed2ad5bd840903c724f935bcd88b61a8838c9e33f09ae00e2e4b7 269 mingw-w64_12.0.0.orig.tar.bz2.asc
 27513ca9c5730bc58eadda089022979b40fa1a5c05893ca3e339345d8c832dba 162596 mingw-w64_12.0.0-5.debian.tar.xz
Files:
 cb32877a300620246926613489022fae 10474283 mingw-w64_12.0.0.orig.tar.bz2
 71f55411ae66182cb859fb3ee1ee7838 269 mingw-w64_12.0.0.orig.tar.bz2.asc
 b8a82df871ecd0bb39648505d2f5c822 162596 mingw-w64_12.0.0-5.debian.tar.xz

-----BEGIN PGP SIGNATURE-----

iQIzBAEBCgAdFiEEnPVX/hPLkMoq7x0ggNMC9Yhtg5wFAmeOe70ACgkQgNMC9Yht
g5y+vhAAk2UeHusVtqqhUDJVWNZUekxBFz7bdaS/hWXQK9X6/R6JVYwejyLqPWDu
KPSeQDoqnjwszVgRWNx5r2dXgX6eq8M2jHwyivfNmcrNkgTsjKIpm7HLNYlk4TlY
W2aJXN+cpaGjpEKuBmOACyy5JEQJNOJSHJXx30Ju5N/Hh86ME2QfVr+2OvOx/QfS
eZodf+guTis9E6QKIXDrUKrB6i+z2O810+CmYi6B9vd3+ZEpL7keglrAUSK7XUY3
+Wsj/OHAjGHelQ3nMZDzn/0OmBXdfJjzQY/GHRMc51sR7FPsljJzGa7jB6qFefWl
vyP5DTSVo8Nf6+zHp+TZWpmfob8Hafo+FmFJqYm2/pWywSfb8qCDPMAKkNoHSvam
s2xWFdPsdwJkkbBmd78BFjAZ8ZcisGDZcX3Wdc8fU6h2rbTsWUzuYC+y5ilDg2q0
OdmiXojKNVSDF/RrCoDbspytXspT0bDX6Mewk3fyKe6iCwrg2HQofAXpP6ZECmMx
mzGx5O5aSVTVVpDilo5PBSdwM86+rURPt2bnrneBX8k3KPaMXg+2Yilzp4y29she
oaUyCVKGTz8keEbFYvcmgu7Ht9EclddGrrdhZcqjk6r4lrP2PrOfMV6qtjBriJDo
gEWfCfYcOqI8p3XcETE9Irfq4Relcc3MsCEQY5Fyk1vLXS+Jn40=
=VoUi
-----END PGP SIGNATURE-----
