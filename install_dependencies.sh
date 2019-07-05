#!/bin/bash


. /etc/os-release

centos_packages=(
  cmake3
  gmp-devel
  numactl-devel
  devtoolset-8
  boost-devel
  bc 
  automake
  pciutils
  wget
  ntp
)

if [ "$ID" = "centos" ] ; then
  yum install -y centos-release-scl epel-release
  rpm --import https://www.elrepo.org/RPM-GPG-KEY-elrepo.org
  yum install -y https://www.elrepo.org/elrepo-release-7.0-3.el7.elrepo.noarch.rpm
  # upgrade kernel in elrepo
  yum install -y --enablerepo elrepo-kernel kernel-lt kernel-lt-devel kernel-lt-headers
  yum install -y "${centos_packets}"
  # install protobuf
  cd downloads
  wget https://github.com/protocolbuffers/protobuf/releases/download/v3.5.0/protobuf-cpp-3.5.0.tar.gz 
  tar xf protobuf-cpp-3.5.0.tar.gz
  cd protobuf-3.5.0
  # this will replace existing c++ compiler in order to compile protobuf and fmt
  ln -s /opt/rh/devtoolset-8/root/bin/c++ /usr/bin/c++
  ./configure
  make
  make install && cd ..

  wget https://github.com/fmtlib/fmt/archive/5.3.0.tar.gz
  tar xf 5.3.0.tar.gz
  cd fmt-5.3.0 && cmake3 ./ && make && make install && cd ../../

  # warning: this will replace current linux kernel
  grub2-set-default 0
  grub2-mkconfig -o /boot/efi/EFI/centos/grub.cfg
  # (or) grub2-mkconfig -o /boot/grub2/grub.cfg
fi




