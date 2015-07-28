/*
 * linux/fs/nvmm/xip.h
 * Copyright (C) 2013 College of Computer Science,
 * Chonqing University
 */


#ifdef CONFIG_NVMM_XIP


#define mapping_is_xip(map) unlikely(map->a_ops->get_xip_mem)

#else

#define mapping_is_xip(map)	0
#define nvmm_use_xip(sb)	0
#define nvmm_get_xip_mem	NULL

#endif

