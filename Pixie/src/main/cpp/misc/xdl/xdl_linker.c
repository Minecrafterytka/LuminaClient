
//






//


//







//



#include "xdl_linker.h"

#include <dlfcn.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>

#include "xdl.h"
#include "xdl_iterate.h"
#include "xdl_util.h"

#define XDL_LINKER_SYM_MUTEX           "__dl__ZL10g_dl_mutex"
#define XDL_LINKER_SYM_DLOPEN_EXT_N    "__dl__ZL10dlopen_extPKciPK17android_dlextinfoPv"
#define XDL_LINKER_SYM_DO_DLOPEN_N     "__dl__Z9do_dlopenPKciPK17android_dlextinfoPv"
#define XDL_LINKER_SYM_DLOPEN_O        "__dl__Z8__dlopenPKciPKv"
#define XDL_LINKER_SYM_LOADER_DLOPEN_P "__loader_dlopen"

typedef void *(*xdl_linker_dlopen_n_t)(const char *, int, const void *, void *);
typedef void *(*xdl_linker_dlopen_o_t)(const char *, int, const void *);

static pthread_mutex_t *xdl_linker_mutex = NULL;
static void *xdl_linker_dlopen = NULL;

static void *xdl_linker_caller_addr[] = {
    NULL,
    NULL,
    NULL
};

#ifndef __LP64__
#define XDL_LINKER_LIB "lib"
#else
#define XDL_LINKER_LIB "lib64"
#endif
static const char *xdl_linker_vendor_path[] = {

    "/vendor/" XDL_LINKER_LIB "/egl/",     "/vendor/" XDL_LINKER_LIB "/hw/",
    "/vendor/" XDL_LINKER_LIB "/",         "/odm/" XDL_LINKER_LIB "/",
    "/vendor/" XDL_LINKER_LIB "/vndk-sp/", "/odm/" XDL_LINKER_LIB "/vndk-sp/"};

static void xdl_linker_init_symbols_impl(void) {

  void *handle = xdl_open(XDL_UTIL_LINKER_BASENAME, XDL_DEFAULT);
  if (NULL == handle) return;

  int api_level = xdl_util_get_api_level();
  if (__ANDROID_API_L__ == api_level || __ANDROID_API_L_MR1__ == api_level) {

    xdl_linker_mutex = (pthread_mutex_t *)xdl_dsym(handle, XDL_LINKER_SYM_MUTEX, NULL);
  } else if (__ANDROID_API_N__ == api_level || __ANDROID_API_N_MR1__ == api_level) {

    xdl_linker_dlopen = xdl_dsym(handle, XDL_LINKER_SYM_DLOPEN_EXT_N, NULL);
    if (NULL == xdl_linker_dlopen) {
      xdl_linker_dlopen = xdl_dsym(handle, XDL_LINKER_SYM_DO_DLOPEN_N, NULL);
      xdl_linker_mutex = (pthread_mutex_t *)xdl_dsym(handle, XDL_LINKER_SYM_MUTEX, NULL);
    }
  } else if (__ANDROID_API_O__ == api_level || __ANDROID_API_O_MR1__ == api_level) {

    xdl_linker_dlopen = xdl_dsym(handle, XDL_LINKER_SYM_DLOPEN_O, NULL);
  } else if (api_level >= __ANDROID_API_P__) {

    xdl_linker_dlopen = xdl_sym(handle, XDL_LINKER_SYM_LOADER_DLOPEN_P, NULL);
  }

  xdl_close(handle);
}

static void xdl_linker_init_symbols(void) {
  static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
  static bool inited = false;
  if (!inited) {
    pthread_mutex_lock(&lock);
    if (!inited) {
      xdl_linker_init_symbols_impl();
      inited = true;
    }
    pthread_mutex_unlock(&lock);
  }
}

void xdl_linker_lock(void) {
  xdl_linker_init_symbols();

  if (NULL != xdl_linker_mutex) pthread_mutex_lock(xdl_linker_mutex);
}

void xdl_linker_unlock(void) {
  if (NULL != xdl_linker_mutex) pthread_mutex_unlock(xdl_linker_mutex);
}

static void *xdl_linker_get_caller_addr(struct dl_phdr_info *info) {
  for (size_t i = 0; i < info->dlpi_phnum; i++) {
    const ElfW(Phdr) *phdr = &(info->dlpi_phdr[i]);
    if (PT_LOAD == phdr->p_type) {
      return (void *)(info->dlpi_addr + phdr->p_vaddr);
    }
  }
  return NULL;
}

static int xdl_linker_get_caller_addr_cb(struct dl_phdr_info *info, size_t size, void *arg) {
  (void)size;

  size_t *vendor_match = (size_t *)arg;

  if (0 == info->dlpi_addr || NULL == info->dlpi_name) return 0;

  if (NULL == xdl_linker_caller_addr[0] && xdl_util_ends_with(info->dlpi_name, "/libc.so"))
    xdl_linker_caller_addr[0] = xdl_linker_get_caller_addr(info);

  if (NULL == xdl_linker_caller_addr[1] && xdl_util_ends_with(info->dlpi_name, "/libart.so"))
    xdl_linker_caller_addr[1] = xdl_linker_get_caller_addr(info);

  if (0 != *vendor_match) {
    for (size_t i = 0; i < *vendor_match; i++) {
      if (xdl_util_starts_with(info->dlpi_name, xdl_linker_vendor_path[i])) {
        void *caller_addr = xdl_linker_get_caller_addr(info);
        if (NULL != caller_addr) {
          xdl_linker_caller_addr[2] = caller_addr;
          *vendor_match = i;
        }
      }
    }
  }

  if (NULL != xdl_linker_caller_addr[0] && NULL != xdl_linker_caller_addr[1] && 0 == *vendor_match) {
    return 1;
  } else {
    return 0;
  }
}

static void xdl_linker_init_caller_addr_impl(void) {
  size_t vendor_match = sizeof(xdl_linker_vendor_path) / sizeof(xdl_linker_vendor_path[0]);
  xdl_iterate_phdr_impl(xdl_linker_get_caller_addr_cb, &vendor_match, XDL_DEFAULT);
}

static void xdl_linker_init_caller_addr(void) {
  static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
  static bool inited = false;
  if (!inited) {
    pthread_mutex_lock(&lock);
    if (!inited) {
      xdl_linker_init_caller_addr_impl();
      inited = true;
    }
    pthread_mutex_unlock(&lock);
  }
}

void *xdl_linker_force_dlopen(const char *filename) {
  int api_level = xdl_util_get_api_level();

  if (api_level <= __ANDROID_API_M__) {

    return dlopen(filename, RTLD_NOW);
  } else {
    xdl_linker_init_symbols();
    if (NULL == xdl_linker_dlopen) return NULL;
    xdl_linker_init_caller_addr();

    void *handle = NULL;
    if (__ANDROID_API_N__ == api_level || __ANDROID_API_N_MR1__ == api_level) {

      xdl_linker_lock();
      for (size_t i = 0; i < sizeof(xdl_linker_caller_addr) / sizeof(xdl_linker_caller_addr[0]); i++) {
        if (NULL != xdl_linker_caller_addr[i]) {
          handle =
              ((xdl_linker_dlopen_n_t)xdl_linker_dlopen)(filename, RTLD_NOW, NULL, xdl_linker_caller_addr[i]);
          if (NULL != handle) break;
        }
      }
      xdl_linker_unlock();
    } else {

      for (size_t i = 0; i < sizeof(xdl_linker_caller_addr) / sizeof(xdl_linker_caller_addr[0]); i++) {
        if (NULL != xdl_linker_caller_addr[i]) {
          handle = ((xdl_linker_dlopen_o_t)xdl_linker_dlopen)(filename, RTLD_NOW, xdl_linker_caller_addr[i]);
          if (NULL != handle) break;
        }
      }
    }
    return handle;
  }
}
