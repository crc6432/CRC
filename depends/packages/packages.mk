packages:= boostclibevent gmp

qt_packages = qrencode zlib

qt_linux_packages:=qt expat dbus libxcb xcb_proto libXau xproto freetype fontconfig

qt_darwin_packages=qt
qt_mingw32_packages=

wallet_packages=bdb

zmq_packages=zeromq

upnp_packages=miniupnpc
natpmp_packages=libnatpmp

darwin_native_packages = native_ds_store native_mac_alias

ifneq ($(build_os),darwin)
darwin_native_packages += native_cctools native_cdrkit native_libdmg-hfsplus
endif
