real_mem=`cat /proc/meminfo | grep MemTotal | sed 's/^[^0-9]*\([0-9]*\).*$/\1/'`
real_cpus=`cat /proc/cpuinfo  | grep processor | wc -l`

mem=$(($real_mem / 1024 / 2))
test $mem -gt 12288 && mem=12288
test $mem -lt 4096 && mem=4096
cpus=$(($real_cpus / 2))
test $cpus -gt 8 && cpus=8
test $cpus -lt 1 && cpus=1

resources="-m $mem -smp $cpus"
dist=xenial
img=xenial-server-cloudimg-amd64-disk1.img

if [[ "$1" != "--pack-only" ]]; then

    test -e $img || wget https://cloud-images.ubuntu.com/$dist/current/$img
    qemu-img create -o backing_file=$img -f qcow2 divine-$dist.qcow2 20G
    qemu-img create -f qcow2 divine-build.qcow2 20G
    mkdir -p cloud-config

    cat > cloud-config/meta-data <<EOF
instance-id: iid-divine-xenial
local-hostname: divine
EOF

    cat > cloud-config/user-data <<EOF
#cloud-config
users:
 - name: divine
   lock_passwd: false
   plain_text_passwd: llvm
   home: /home/divine
   sudo: ALL=(ALL) NOPASSWD:ALL
runcmd:
 - apt-get update
 - apt-get install -y make
 - mkfs.ext4 /dev/sdb
 - mount /dev/sdb /mnt
 - cd /mnt
 - "curl https://divine.fi.muni.cz/install.sh | sh"
 - "curl https://divine.fi.muni.cz/install-vm.sh | sh"
 - apt-get remove -y cloud-init
 - poweroff
EOF

    genisoimage -V cidata -D -J -joliet-long -o config.iso cloud-config
    rm -rf cloud-config

    cdrom="-drive file=config.iso,media=cdrom"

    vboxadditions=/usr/lib/virtualbox/additions/VBoxGuestAdditions.iso
    if test -f $vboxadditions; then
        cdrom="$cdrom -drive file=$vboxadditions,media=cdrom"
    fi

    qemu-system-x86_64 -machine accel=kvm -display none -serial stdio \
                       -hda divine-$dist.qcow2 -hdb divine-build.qcow2 \
                       $cdrom $resources

    rm -f config.iso

fi

if [[ "$1" != "--skip-vbox" ]]; then
    version=$(curl -s https://divine.fi.muni.cz/install.sh | grep version= | head -n1 | sed 's/version=//')

    base=divine-$version
    ova=$base.ova
    vdi=$base.vdi
    vm=$base
    qemu-img convert -O vdi divine-xenial.qcow2 $vdi
    rm -rf _vbox
    mkdir _vbox
    VBoxManage unregistervm $vm --delete >& /dev/null
    VBoxManage createvm --name $vm --ostype Ubuntu_64 --basefolder $PWD/_vbox --register
    VBoxManage storagectl $vm --name "SATA Controller" --add sata --controller IntelAHCI
    VBoxManage storageattach $vm --storagectl "SATA Controller" --port 0 \
                                 --device 0 --type hdd --medium $vdi
    VBoxManage modifyvm $vm --memory 4096 --cpus 2 --nic1 nat \
                            --uart1 0x3F8 4 --uartmode1 disconnected
    rm -f $ova
    VBoxManage export $vm -o $ova --vsys 0 \
                          --product "DIVINE $version" \
                          --producturl "https://divine.fi.muni.cz" \
                          --vendor "ParaDiSe Lab, FI MU, CZ" \
                          --vendorurl "https://paradise.fi.muni.cz"
    rm -f $vdi.xz
    xz -vk -9 $vdi
    VBoxManage unregistervm $vm --delete
    rm -rf _vbox
    chmod go=r $ova
fi
