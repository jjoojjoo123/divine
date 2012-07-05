{stdenv, fetchurl, writeScript}:
 
let mingw = { pkg, hash }: fetchurl { url = "mirror://sourceforge/mingw/${pkg}"; sha256 = hash; };
 in stdenv.mkDerivation rec {
  name = "windows-mingw";

  builder = writeScript "${name}-builder" ''
    source $stdenv/setup
    mkdir -p $out
    mkdir mingw && cd mingw
    for f in $pkgs; do
       tar xaf $f
    done
    echo "D:\\ /tools" > etc/fstab
    cd ..
    tar czf $out/package.tar.gz mingw
  '';

  unpack = "tar xzf $package/package.tar.gz";
  install = "set PATH=%PATH%;D:\\mingw\\bin";

  pkgs = [ libgmp libintl_msys libintl_mingw libmpc libmpfr libiconv
           libregex libtermcap libgcc regex_dev tar
           gcc_core gcc_cpp binutils bash make msys_core perl
           zlib libbz2 libcrypt libexpat sed grep coreutils
           w32api mingw_rt ];

  libgmp = mingw {
    pkg = "libgmp-5.0.1-1-mingw32-dll-10.tar.lzma";
    hash = "0li69xmj2jj7yh2h19gwffjbzfaq45ckxn6wn6nd6a3g3lyn9q13"; };
  zlib = mingw {
    pkg = "zlib-1.2.3-2-msys-1.0.13-dll.tar.lzma";
    hash = "1lf0m2kg4mg5qdnyzspzp6zv94mrzf1wqhihlgav4a5r50498y21"; };
  libcrypt = mingw {
    pkg = "libcrypt-1.1_1-3-msys-1.0.13-dll-0.tar.lzma";
    hash = "067rjzswpfwwidkvccw8gviyj2cyp2w069b70ya8829mk6v5gw9i"; };
  libexpat = mingw {
    pkg = "libexpat-2.0.1-1-mingw32-dll-1.tar.gz";
    hash = "08rpm2hx4jbliqa5hg8zwy7figdpmbiqid8zrr14xgzc5smmflad"; };
  libbz2 = mingw {
    pkg = "libbz2-1.0.5-2-msys-1.0.13-dll-1.tar.lzma";
    hash = "0ksk6wh1lbm4dp7myjf1li1nij789c6fxdl4q6xnliy7qii0inks"; };
  regex_dev = mingw {
    pkg = "libregex-1.20090805-2-msys-1.0.13-dev.tar.lzma";
    hash = "1hz17j8wgr5px0k6vfi41bj3xscl9ff07s26890iwi4mqmrwcggq"; };
  sed = mingw {
    pkg = "sed-4.2.1-2-msys-1.0.13-bin.tar.lzma";
    hash = "150vbp7fs3kf6aif7ip5q18bzkmzrg0bf201hkkr3dpc9hh5jc7p"; };
  grep = mingw {
    pkg = "grep-2.5.4-2-msys-1.0.13-bin.tar.lzma";
    hash = "1fj00k4nmrri71fwsy2pw1vwxyx7psrzm3rfcaabk3gr9mss2hj8"; };
  tar = mingw {
    pkg = "tar-1.23-1-msys-1.0.13-bin.tar.lzma";
    hash = "11r818jkd3sfxflv9r6v2vj9vy69j5l2fnx6fy061ab3jmd9x81g"; };
  coreutils = mingw {
    pkg = "coreutils-5.97-3-msys-1.0.13-bin.tar.lzma";
    hash = "0ri31ci1952hlgw8rmdj4j3npi3bb47zxp1nqd5af5pa2q29kizq"; };
  libintl_mingw = mingw {
    pkg = "libintl-0.18.1.1-2-mingw32-dll-8.tar.lzma";
    hash = "0jandbdd0ijd6gzbafxb9s35931clzr9pz1zvdnvqg933h5zk40k"; };
  libintl_msys = mingw {
    pkg = "libintl-0.18.1.1-1-msys-1.0.17-dll-8.tar.lzma";
    hash = "0439q7g71ymwvpb7aal4ap2zkickr4jsnhd3wbxi3ib1jsb8rnr9"; };
  libmpc = mingw {
    pkg = "libmpc-0.8.1-1-mingw32-dll-2.tar.lzma";
    hash = "1dim364a05gyrkjgmb194fnqsb0b4qz8rssk8dx795jym0vbqivh"; };
  libmpfr = mingw {
    pkg = "libmpfr-2.4.1-1-mingw32-dll-1.tar.lzma";
    hash = "0h8a8g10d5dd8sja83ncccms3yx12mfxc1v7h2gwsywvbs2nm8v4"; };
  libiconv = mingw {
    pkg = "libiconv-1.14-1-msys-1.0.17-dll-2.tar.lzma";
    hash = "10qn4csg19kvqa6hr7s1j6dd5clsippbjlk8da79q99jqbl22s8r"; };
  libregex = mingw {
    pkg = "libregex-1.20090805-2-msys-1.0.13-dll-1.tar.lzma";
    hash = "1lfk9c9vx5n8hkr1jbfdx3fmbfz2gax5ggk7z32pa1m94wg8rpc5"; };
  libgcc = mingw {
    pkg = "libgcc-4.6.2-1-mingw32-dll-1.tar.lzma";
    hash = "13mpyc2hlk277d43n6504d32ibd4jihdcbny2klvcymhx477lmfs"; };
  libtermcap = mingw {
    pkg = "libtermcap-0.20050421_1-2-msys-1.0.13-dll-0.tar.lzma";
    hash = "1dmdyxqgh9dx4w18g1ai3jf460lvk1jr50aar3y7428gi3h8zdb2"; };
  gcc_core = mingw {
    pkg = "gcc-core-4.6.2-1-mingw32-bin.tar.lzma";
    hash = "05rf0hi70bingg8041vfqayknpkxi6vz2a8hi9b2fcvl34n0fj6r"; };
  gcc_cpp = mingw {
    pkg = "gcc-c++-4.6.2-1-mingw32-bin.tar.lzma";
    hash = "0q0kx3nf2avks9iys2dg8fhhrl4v1q2dz3s82kw2wwf1ssm5bn6z"; };
  binutils = mingw {
    pkg = "binutils-2.22-1-mingw32-bin.tar.lzma";
    hash = "0a9cq0kpkb53b4fzxjhk93zcjpl1czj4adihv7n7gf3jj2in7n35"; };
  bash = mingw {
    pkg = "bash-3.1.17-4-msys-1.0.16-bin.tar.lzma";
    hash = "09h1y7fc5jpwlyy0pzj2159ajdzs6vdx9j7xnsyqz30kfzgi3snn"; };
  make = mingw {
    pkg = "make-3.81-3-msys-1.0.13-bin.tar.lzma";
    hash = "124rp2lnka3zk4y7aa6qda0ix0mi54nnkgv7iqf80dbiy2xhqzw4"; };
  msys_core = mingw {
    pkg = "msysCORE-1.0.17-1-msys-1.0.17-bin.tar.lzma";
    hash = "0zq3ichd7vqrbd5fx9383znq66mysj3dizx1303plygmjkiplw1d"; };
  perl = mingw {
    pkg = "perl-5.8.8-1-msys-1.0.17-bin.tar.lzma";
    hash = "16bs69m018b9ri6qzsbx09klyw75rq4aklh5841xswh1w2f96ywq"; };
  w32api = mingw {
    pkg = "w32api-3.17-2-mingw32-dev.tar.lzma";
    hash = "02h936464fflddcmcq4j5yizq12b75w4772im923pcgdkdvn0av7"; };
  mingw_rt = mingw {
    pkg = "mingwrt-3.20-mingw32-dev.tar.gz";
    hash = "1wi850pirpc10sfrvmglg11gkh5vmr53hkh3lc42agh9371if5x4"; };

}
