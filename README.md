<!-- SPDX-License-Identifier: zlib-acknowledgement -->

# HHF (Handmade Hero Fork)
> ![Github](https://img.shields.io/github/license/ryan-mcclue/hhf)

An Ubuntu/Linux fork of Casey Muratori's brilliant [Handmade Hero](https://handmadehero.org/)

## Executables
```
xargs -a misc/requirements.txt sudo apt install 

sh misc/build
./build/ubuntu-hhf
```

## Packaging
[Debian Package Repo](https://github.com/ryan-mcclue/hhf-deb)
```
# Install
sudo add-apt-repository ppa:ryan-mcclue/personal
sudo apt update
sudo apt install hhf

# Update
sudo apt list --upgradeable | grep hhf
sudo apt --only-upgrade install hhf
```

## Attributions
<div>Icons made by <a href="https://www.freepik.com" title="Freepik">Freepik</a> from <a href="https://www.flaticon.com/" title="Flaticon">www.flaticon.com</a></div>
