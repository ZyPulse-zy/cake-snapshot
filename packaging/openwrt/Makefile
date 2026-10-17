include $(TOPDIR)/rules.mk

PKG_NAME:=cake-snapshot
PKG_VERSION:=0.1.0
PKG_RELEASE:=1
PKG_LICENSE:=GPL-2.0-or-later
PKG_LICENSE_FILES:=COPYING
PKG_MAINTAINER:=Yuen Su <zypulsecore@gmail.com>

include $(INCLUDE_DIR)/package.mk

define Package/cake-snapshot
  SECTION:=net
  CATEGORY:=Network
  TITLE:=Single-dump read-only root CAKE bandwidth reader
endef

define Package/cake-snapshot/description
 Read exact finite root CAKE rates for multiple interfaces in one validated
 rtnetlink dump. No configuration, service or network changes are performed.
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) ./src/cake_snapshot.c ./COPYING $(PKG_BUILD_DIR)/
endef

define Build/Compile
	$(TARGET_CC) $(TARGET_CPPFLAGS) $(TARGET_CFLAGS) -std=c11 \
		-Wall -Wextra -Werror $(PKG_BUILD_DIR)/cake_snapshot.c \
		$(TARGET_LDFLAGS) -o $(PKG_BUILD_DIR)/cake-snapshot
endef

define Package/cake-snapshot/install
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/cake-snapshot $(1)/usr/bin/
endef

$(eval $(call BuildPackage,cake-snapshot))
