ifndef inc_bluetooth_btstack
inc_bluetooth_btstack := 1

ifndef iOSAppStore

include $(IMAGINE_PATH)/make/package/btstack.mk

include $(imagineSrcDir)/bluetooth/bluetooth.mk

SRC += bluetooth/BtstackBluetoothAdapter.cc

endif

endif
