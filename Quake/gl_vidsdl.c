/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2009 John Fitzgibbons and others
Copyright (C) 2007-2008 Kristian Duske
Copyright (C) 2010-2014 QuakeSpasm developers
Copyright (C) 2016 Axel Gneiting

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// gl_vidsdl.c -- SDL vid component

#include "quakedef.h"
#define NO_SDL_VULKAN_TYPEDEFS
#include "cfgfile.h"
#include "bgmusic.h"
#include "resource.h"
#include "palette.h"
#include "SDL.h"
#include "SDL_syswm.h"
#include "SDL_vulkan.h"
#ifdef _WIN32
#include <vulkan/vulkan_win32.h>
#endif

#define MAX_MODE_LIST  600 // johnfitz -- was 30
#define MAX_BPPS_LIST  5
#define MAX_RATES_LIST 20
#define MAXWIDTH	   10000
#define MAXHEIGHT	   10000

#define MAX_SWAP_CHAIN_IMAGES 8
#define REQUIRED_COLOR_BUFFER_FEATURES                                                                                             \
	(VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | \
	 VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)

#define DEFAULT_REFRESHRATE 60

typedef struct
{
	int width;
	int height;
	int refreshrate;
} vmode_t;

static vmode_t *modelist = NULL;
static int		nummodes;

static qboolean vid_initialized = false;
static qboolean has_focus = true;
static uint32_t num_images_acquired = 0;

static SDL_Window	*draw_context;
static SDL_SysWMinfo sys_wm_info;

static qboolean vid_locked = false; // johnfitz
static qboolean vid_changed = false;

static void VID_Menu_Init (void); // johnfitz
static void VID_Menu_f (void);	  // johnfitz
static void VID_MenuDraw (cb_context_t *cbx);
static void VID_MenuKey (int key);
static void VID_Restart (qboolean set_mode);
static void VID_Restart_f (void);

static void ClearAllStates (void);
static void GL_InitInstance (void);
static void GL_InitDevice (void);
static void GL_CreateFrameBuffers (void);
static void GL_DestroyRenderResources (void);

viddef_t		vid; // global video state
modestate_t		modestate = MS_UNINIT;
extern qboolean scr_initialized;
extern cvar_t	r_particles, host_maxfps, r_gpulightmapupdate, r_showtris, r_showbboxes;

//====================================

// johnfitz -- new cvars
static cvar_t					vid_fullscreen = {"vid_fullscreen", "0", CVAR_ARCHIVE}; // QuakeSpasm, was "1"
static cvar_t					vid_width = {"vid_width", "1280", CVAR_ARCHIVE};		// QuakeSpasm, was 640
static cvar_t					vid_height = {"vid_height", "720", CVAR_ARCHIVE};		// QuakeSpasm, was 480
static cvar_t					vid_refreshrate = {"vid_refreshrate", "60", CVAR_ARCHIVE};
static cvar_t					vid_vsync = {"vid_vsync", "0", CVAR_ARCHIVE};
static cvar_t					vid_desktopfullscreen = {"vid_desktopfullscreen", "0", CVAR_ARCHIVE}; // QuakeSpasm
static cvar_t					vid_borderless = {"vid_borderless", "0", CVAR_ARCHIVE};				  // QuakeSpasm
cvar_t							vid_palettize = {"vid_palettize", "0", CVAR_ARCHIVE};
cvar_t							vid_filter = {"vid_filter", "0", CVAR_ARCHIVE};
cvar_t							vid_anisotropic = {"vid_anisotropic", "0", CVAR_ARCHIVE};
cvar_t							vid_fsaa = {"vid_fsaa", "0", CVAR_ARCHIVE};
cvar_t							vid_fsaamode = {"vid_fsaamode", "0", CVAR_ARCHIVE};
cvar_t							vid_gamma = {"gamma", "0.9", CVAR_ARCHIVE};		  // johnfitz -- moved here from view.c
cvar_t							vid_contrast = {"contrast", "1.4", CVAR_ARCHIVE}; // QuakeSpasm, MarkV
cvar_t							r_usesops = {"r_usesops", "1", CVAR_ARCHIVE};	  // johnfitz
																				  // Vulkan
static VkInstance				vulkan_instance;
static VkPhysicalDevice			vulkan_physical_device;
static VkPhysicalDeviceFeatures vulkan_physical_device_features;
static VkSurfaceKHR				vulkan_surface;
static VkSurfaceCapabilitiesKHR vulkan_surface_capabilities;
static VkSwapchainKHR			vulkan_swapchain;

static uint32_t		   num_swap_chain_images;
static qboolean		   render_resources_created = false;
static uint32_t		   current_cb_index;
static VkCommandPool   primary_command_pool;
static VkCommandPool   secondary_command_pools[CBX_NUM];
static VkCommandPool   transient_command_pool;
static VkCommandBuffer primary_command_buffers[DOUBLE_BUFFERED];
static VkCommandBuffer secondary_command_buffers[CBX_NUM][DOUBLE_BUFFERED];
static VkFence		   command_buffer_fences[DOUBLE_BUFFERED];
static qboolean		   frame_submitted[DOUBLE_BUFFERED];
static VkFramebuffer   main_framebuffers[NUM_COLOR_BUFFERS];
static VkSemaphore	   image_aquired_semaphores[DOUBLE_BUFFERED];
static VkSemaphore	   draw_complete_semaphores[DOUBLE_BUFFERED];
static VkFramebuffer   ui_framebuffers[MAX_SWAP_CHAIN_IMAGES];
static VkImage		   swapchain_images[MAX_SWAP_CHAIN_IMAGES];
static VkImageView	   swapchain_images_views[MAX_SWAP_CHAIN_IMAGES];
static VkImage		   depth_buffer;
static vulkan_memory_t depth_buffer_memory;
static VkImageView	   depth_buffer_view;
static vulkan_memory_t color_buffers_memory[NUM_COLOR_BUFFERS];
static VkImageView	   color_buffers_view[NUM_COLOR_BUFFERS];
static VkImage		   msaa_color_buffer;
static vulkan_memory_t msaa_color_buffer_memory;
static VkImageView	   msaa_color_buffer_view;
static VkDescriptorSet postprocess_descriptor_set;
static VkBuffer		   palette_colors_buffer;
static VkBufferView	   palette_buffer_view;
static VkBuffer		   palette_octree_buffer;

static PFN_vkGetInstanceProcAddr					  fpGetInstanceProcAddr;
static PFN_vkGetDeviceProcAddr						  fpGetDeviceProcAddr;
static PFN_vkGetPhysicalDeviceSurfaceSupportKHR		  fpGetPhysicalDeviceSurfaceSupportKHR;
static PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR  fpGetPhysicalDeviceSurfaceCapabilitiesKHR;
static PFN_vkGetPhysicalDeviceSurfaceCapabilities2KHR fpGetPhysicalDeviceSurfaceCapabilities2KHR;
static PFN_vkGetPhysicalDeviceSurfaceFormatsKHR		  fpGetPhysicalDeviceSurfaceFormatsKHR;
static PFN_vkGetPhysicalDeviceSurfacePresentModesKHR  fpGetPhysicalDeviceSurfacePresentModesKHR;
static PFN_vkCreateSwapchainKHR						  fpCreateSwapchainKHR;
static PFN_vkDestroySwapchainKHR					  fpDestroySwapchainKHR;
static PFN_vkGetSwapchainImagesKHR					  fpGetSwapchainImagesKHR;
static PFN_vkAcquireNextImageKHR					  fpAcquireNextImageKHR;
static PFN_vkQueuePresentKHR						  fpQueuePresentKHR;
static PFN_vkEnumerateInstanceVersion				  fpEnumerateInstanceVersion;
#if defined(VK_EXT_full_screen_exclusive)
static PFN_vkAcquireFullScreenExclusiveModeEXT fpAcquireFullScreenExclusiveModeEXT;
static PFN_vkReleaseFullScreenExclusiveModeEXT fpReleaseFullScreenExclusiveModeEXT;
#endif

#ifdef _DEBUG
static PFN_vkCreateDebugUtilsMessengerEXT fpCreateDebugUtilsMessengerEXT;
PFN_vkSetDebugUtilsObjectNameEXT		  fpSetDebugUtilsObjectNameEXT;

VkDebugUtilsMessengerEXT debug_utils_messenger;

VkBool32 VKAPI_PTR DebugMessageCallback (
	VkDebugUtilsMessageSeverityFlagBitsEXT message_severity, VkDebugUtilsMessageTypeFlagsEXT message_types,
	const VkDebugUtilsMessengerCallbackDataEXT *callback_data, void *user_data)
{
	Sys_Printf ("%s\n", callback_data->pMessage);
	return VK_FALSE;
}
#endif

// Swap chain
static uint32_t current_swapchain_buffer;

task_handle_t prev_end_rendering_task = INVALID_TASK_HANDLE;

#define GET_INSTANCE_PROC_ADDR(entrypoint)                                                              \
	{                                                                                                   \
		fp##entrypoint = (PFN_vk##entrypoint)fpGetInstanceProcAddr (vulkan_instance, "vk" #entrypoint); \
		if (fp##entrypoint == NULL)                                                                     \
			Sys_Error ("vkGetInstanceProcAddr failed to find vk" #entrypoint);                          \
	}

#define GET_GLOBAL_INSTANCE_PROC_ADDR(_var, entrypoint)                                               \
	{                                                                                                 \
		vulkan_globals._var = (PFN_##entrypoint)fpGetInstanceProcAddr (vulkan_instance, #entrypoint); \
		if (vulkan_globals._var == NULL)                                                              \
			Sys_Error ("vkGetInstanceProcAddr failed to find " #entrypoint);                          \
	}

#define GET_DEVICE_PROC_ADDR(entrypoint)                                                                    \
	{                                                                                                       \
		fp##entrypoint = (PFN_vk##entrypoint)fpGetDeviceProcAddr (vulkan_globals.device, "vk" #entrypoint); \
		if (fp##entrypoint == NULL)                                                                         \
			Sys_Error ("vkGetDeviceProcAddr failed to find vk" #entrypoint);                                \
	}

#define GET_GLOBAL_DEVICE_PROC_ADDR(_var, entrypoint)                                                     \
	{                                                                                                     \
		vulkan_globals._var = (PFN_##entrypoint)fpGetDeviceProcAddr (vulkan_globals.device, #entrypoint); \
		if (vulkan_globals._var == NULL)                                                                  \
			Sys_Error ("vkGetDeviceProcAddr failed to find " #entrypoint);                                \
	}
/*
================
VID_Gamma_Init -- call on init
================
*/
static void VID_Gamma_Init (void)
{
	Cvar_RegisterVariable (&vid_gamma);
	Cvar_RegisterVariable (&vid_contrast);
}

/*
======================
VID_GetCurrentWidth
======================
*/
static int VID_GetCurrentWidth (void)
{
	int w = 0, h = 0;
	SDL_Vulkan_GetDrawableSize (draw_context, &w, &h);
	return w;
}

/*
=======================
VID_GetCurrentHeight
=======================
*/
static int VID_GetCurrentHeight (void)
{
	int w = 0, h = 0;
	SDL_Vulkan_GetDrawableSize (draw_context, &w, &h);
	return h;
}

/*
====================
VID_GetCurrentRefreshRate
====================
*/
static int VID_GetCurrentRefreshRate (void)
{
	SDL_DisplayMode mode;
	int				current_display;

	current_display = SDL_GetWindowDisplayIndex (draw_context);

	if (0 != SDL_GetCurrentDisplayMode (current_display, &mode))
		return DEFAULT_REFRESHRATE;

	return mode.refresh_rate;
}

/*
====================
VID_GetCurrentBPP
====================
*/
static int VID_GetCurrentBPP (void)
{
	const Uint32 pixelFormat = SDL_GetWindowPixelFormat (draw_context);
	return SDL_BITSPERPIXEL (pixelFormat);
}

/*
====================
VID_GetFullscreen

returns true if we are in regular fullscreen or "desktop fullscren"
====================
*/
static qboolean VID_GetFullscreen (void)
{
	return (SDL_GetWindowFlags (draw_context) & SDL_WINDOW_FULLSCREEN) != 0;
}

/*
====================
VID_GetDesktopFullscreen

returns true if we are specifically in "desktop fullscreen" mode
====================
*/
static qboolean VID_GetDesktopFullscreen (void)
{
	return (SDL_GetWindowFlags (draw_context) & SDL_WINDOW_FULLSCREEN_DESKTOP) == SDL_WINDOW_FULLSCREEN_DESKTOP;
}

/*
====================
VID_GetWindow

used by pl_win.c
====================
*/
void *VID_GetWindow (void)
{
	return draw_context;
}

/*
====================
VID_HasMouseOrInputFocus
====================
*/
qboolean VID_HasMouseOrInputFocus (void)
{
	return (SDL_GetWindowFlags (draw_context) & (SDL_WINDOW_MOUSE_FOCUS | SDL_WINDOW_INPUT_FOCUS)) != 0;
}

/*
====================
VID_IsMinimized
====================
*/
qboolean VID_IsMinimized (void)
{
	return !(SDL_GetWindowFlags (draw_context) & SDL_WINDOW_SHOWN);
}

/*
================
VID_SDL2_GetDisplayMode

Returns a pointer to a statically allocated SDL_DisplayMode structure
if there is one with the requested params on the default display.
Otherwise returns NULL.

This is passed to SDL_SetWindowDisplayMode to specify a pixel format
with the requested bpp. If we didn't care about bpp we could just pass NULL.
================
*/
static SDL_DisplayMode *VID_SDL2_GetDisplayMode (int width, int height, int refreshrate)
{
	static SDL_DisplayMode mode;
	const int			   sdlmodes = SDL_GetNumDisplayModes (0);
	int					   i;

	for (i = 0; i < sdlmodes; i++)
	{
		if (SDL_GetDisplayMode (0, i, &mode) != 0)
			continue;

		if (mode.w == width && mode.h == height && SDL_BITSPERPIXEL (mode.format) >= 24 && mode.refresh_rate == refreshrate)
		{
			return &mode;
		}
	}
	return NULL;
}

/*
================
VID_ValidMode
================
*/
static qboolean VID_ValidMode (int width, int height, int refreshrate, qboolean fullscreen)
{
	// ignore width / height / bpp if vid_desktopfullscreen is enabled
	if (fullscreen && vid_desktopfullscreen.value)
		return true;

	if (width < 320)
		return false;

	if (height < 200)
		return false;

	if (fullscreen && VID_SDL2_GetDisplayMode (width, height, refreshrate) == NULL)
		return false;

	return true;
}

/*
================
VID_SetMode
================
*/
static qboolean VID_SetMode (int width, int height, int refreshrate, qboolean fullscreen)
{
	int	   temp;
	Uint32 flags;
	char   caption[50];
	int	   previous_display;

	// so Con_Printfs don't mess us up by forcing vid and snd updates
	temp = scr_disabled_for_loading;
	scr_disabled_for_loading = true;

	CDAudio_Pause ();
	BGM_Pause ();

	q_snprintf (caption, sizeof (caption), "vkQuake " VKQUAKE_VER_STRING);

	/* Create the window if needed, hidden */
	if (!draw_context)
	{
		flags = SDL_WINDOW_HIDDEN | SDL_WINDOW_VULKAN;

		if (vid_borderless.value)
			flags |= SDL_WINDOW_BORDERLESS;
		else if (!fullscreen)
			flags |= SDL_WINDOW_RESIZABLE;

		draw_context = SDL_CreateWindow (caption, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, width, height, flags);
		if (!draw_context)
			Sys_Error ("Couldn't create window: %s", SDL_GetError ());

		SDL_VERSION (&sys_wm_info.version);
		if (!SDL_GetWindowWMInfo (draw_context, &sys_wm_info))
			Sys_Error ("Couldn't get window wm info: %s", SDL_GetError ());

		previous_display = -1;
	}
	else
	{
		previous_display = SDL_GetWindowDisplayIndex (draw_context);
	}

	/* Ensure the window is not fullscreen */
	if (VID_GetFullscreen ())
	{
		if (SDL_SetWindowFullscreen (draw_context, 0) != 0)
			Sys_Error ("Couldn't set fullscreen state mode: %s", SDL_GetError ());
	}

	/* Set window size and display mode */
	SDL_SetWindowSize (draw_context, width, height);
	if (previous_display >= 0)
		SDL_SetWindowPosition (draw_context, SDL_WINDOWPOS_CENTERED_DISPLAY (previous_display), SDL_WINDOWPOS_CENTERED_DISPLAY (previous_display));
	else
		SDL_SetWindowPosition (draw_context, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
	SDL_SetWindowDisplayMode (draw_context, VID_SDL2_GetDisplayMode (width, height, refreshrate));
	SDL_SetWindowBordered (draw_context, vid_borderless.value ? SDL_FALSE : SDL_TRUE);

	/* Make window fullscreen if needed, and show the window */

	if (fullscreen)
	{
		const Uint32 flag = vid_desktopfullscreen.value ? SDL_WINDOW_FULLSCREEN_DESKTOP : SDL_WINDOW_FULLSCREEN;
		if (SDL_SetWindowFullscreen (draw_context, flag) != 0)
			Sys_Error ("Couldn't set fullscreen state mode: %s", SDL_GetError ());
	}

	SDL_ShowWindow (draw_context);
	SDL_RaiseWindow (draw_context);

	vid.width = VID_GetCurrentWidth ();
	vid.height = VID_GetCurrentHeight ();
	vid.conwidth = vid.width & 0xFFFFFFF8;
	vid.conheight = vid.conwidth * vid.height / vid.width;

	modestate = VID_GetFullscreen () ? MS_FULLSCREEN : MS_WINDOWED;

	CDAudio_Resume ();
	BGM_Resume ();
	scr_disabled_for_loading = temp;

	// fix the leftover Alt from any Alt-Tab or the like that switched us away
	ClearAllStates ();

	vid.recalc_refdef = 1;

	// no pending changes
	vid_changed = false;

	SCR_UpdateRelativeScale ();

	return true;
}

/*
===================
VID_Changed_f -- kristian -- notify us that a value has changed that requires a vid_restart
===================
*/
static void VID_Changed_f (cvar_t *var)
{
	vid_changed = true;
}

/*
===================
VID_FilterChanged_f
===================
*/
static void VID_FilterChanged_f (cvar_t *var)
{
	R_InitSamplers ();
}

/*
================
VID_Test -- johnfitz -- like vid_restart, but asks for confirmation after switching modes
================
*/
static void VID_Test (void)
{
	int old_width, old_height, old_refreshrate, old_fullscreen;

	if (vid_locked || !vid_changed)
		return;
	//
	// now try the switch
	//
	old_width = VID_GetCurrentWidth ();
	old_height = VID_GetCurrentHeight ();
	old_refreshrate = VID_GetCurrentRefreshRate ();
	old_fullscreen = VID_GetFullscreen () ? (vulkan_globals.swap_chain_full_screen_exclusive ? 2 : 1) : 0;
	VID_Restart (true);

	// pop up confirmation dialoge
	if (!SCR_ModalMessage ("Would you like to keep this\nvideo mode? (y/n)\n", 5.0f))
	{
		// revert cvars and mode
		Cvar_SetValueQuick (&vid_width, old_width);
		Cvar_SetValueQuick (&vid_height, old_height);
		Cvar_SetValueQuick (&vid_refreshrate, old_refreshrate);
		Cvar_SetValueQuick (&vid_fullscreen, old_fullscreen);
		VID_Restart (true);
	}
}

/*
================
VID_Unlock -- johnfitz
================
*/
static void VID_Unlock (void)
{
	vid_locked = false;
	VID_SyncCvars ();
}

/*
================
VID_Lock -- ericw

Subsequent changes to vid_* mode settings, and vid_restart commands, will
be ignored until the "vid_unlock" command is run.

Used when changing gamedirs so the current settings override what was saved
in the config.cfg.
================
*/
void VID_Lock (void)
{
	vid_locked = true;
}

//==============================================================================
//
//	Vulkan Stuff
//
//==============================================================================

/*
===============
GL_SetObjectName
===============
*/
void GL_SetObjectName (uint64_t object, VkObjectType object_type, const char *name)
{
#ifdef _DEBUG
	if (fpSetDebugUtilsObjectNameEXT && name)
	{
		VkDebugUtilsObjectNameInfoEXT nameInfo;
		memset (&nameInfo, 0, sizeof (nameInfo));
		nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
		nameInfo.objectType = object_type;
		nameInfo.objectHandle = object;
		nameInfo.pObjectName = name;
		fpSetDebugUtilsObjectNameEXT (vulkan_globals.device, &nameInfo);
	};
#endif
}

/*
===============
GL_InitInstance
===============
*/
static void GL_InitInstance (void)
{
	VkResult	 err;
	uint32_t	 i;
	unsigned int sdl_extension_count;
	vulkan_globals.debug_utils = false;

	if (!SDL_Vulkan_GetInstanceExtensions (draw_context, &sdl_extension_count, NULL))
		Sys_Error ("SDL_Vulkan_GetInstanceExtensions failed: %s", SDL_GetError ());

	const char **const instance_extensions = Mem_Alloc (sizeof (const char *) * (sdl_extension_count + 3));
	if (!SDL_Vulkan_GetInstanceExtensions (draw_context, &sdl_extension_count, instance_extensions))
		Sys_Error ("SDL_Vulkan_GetInstanceExtensions failed: %s", SDL_GetError ());

	uint32_t instance_extension_count;
	err = vkEnumerateInstanceExtensionProperties (NULL, &instance_extension_count, NULL);

	uint32_t additionalExtensionCount = 0;

	vulkan_globals.get_surface_capabilities_2 = false;
	vulkan_globals.get_physical_device_properties_2 = false;
	if (err == VK_SUCCESS || instance_extension_count > 0)
	{
		VkExtensionProperties *extension_props = (VkExtensionProperties *)Mem_Alloc (sizeof (VkExtensionProperties) * instance_extension_count);
		err = vkEnumerateInstanceExtensionProperties (NULL, &instance_extension_count, extension_props);

		for (i = 0; i < instance_extension_count; ++i)
		{
			if (strcmp (VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME, extension_props[i].extensionName) == 0)
				vulkan_globals.get_surface_capabilities_2 = true;
			if (strcmp (VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME, extension_props[i].extensionName) == 0)
				vulkan_globals.get_physical_device_properties_2 = true;
#if _DEBUG
			if (strcmp (VK_EXT_DEBUG_UTILS_EXTENSION_NAME, extension_props[i].extensionName) == 0)
				vulkan_globals.debug_utils = true;
#endif
		}

		Mem_Free (extension_props);
	}

	vulkan_globals.vulkan_1_1_available = false;
	fpGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)SDL_Vulkan_GetVkGetInstanceProcAddr ();
	GET_INSTANCE_PROC_ADDR (EnumerateInstanceVersion);
	if (fpEnumerateInstanceVersion)
	{
		uint32_t api_version = 0;
		fpEnumerateInstanceVersion (&api_version);
		if (api_version >= VK_MAKE_VERSION (1, 1, 0))
		{
			Con_Printf ("Using Vulkan 1.1\n");
			vulkan_globals.vulkan_1_1_available = true;
		}
	}

	VkApplicationInfo application_info;
	memset (&application_info, 0, sizeof (application_info));
	application_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	application_info.pApplicationName = "vkQuake";
	application_info.applicationVersion = 1;
	application_info.pEngineName = "vkQuake";
	application_info.engineVersion = 1;
	application_info.apiVersion = vulkan_globals.vulkan_1_1_available ? VK_MAKE_VERSION (1, 1, 0) : VK_MAKE_VERSION (1, 0, 0);

	VkInstanceCreateInfo instance_create_info;
	memset (&instance_create_info, 0, sizeof (instance_create_info));
	instance_create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	instance_create_info.pApplicationInfo = &application_info;
	instance_create_info.ppEnabledExtensionNames = instance_extensions;

	if (vulkan_globals.get_surface_capabilities_2)
		instance_extensions[sdl_extension_count + additionalExtensionCount++] = VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME;
	if (vulkan_globals.get_physical_device_properties_2)
		instance_extensions[sdl_extension_count + additionalExtensionCount++] = VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME;

#ifdef _DEBUG
	if (vulkan_globals.debug_utils)
		instance_extensions[sdl_extension_count + additionalExtensionCount++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;

	const char *const layer_names[] = {"VK_LAYER_KHRONOS_validation"};
	if (vulkan_globals.validation)
	{
		Con_Printf ("Using VK_LAYER_KHRONOS_validation\n");
		instance_create_info.enabledLayerCount = 1;
		instance_create_info.ppEnabledLayerNames = layer_names;
	}
#endif

	instance_create_info.enabledExtensionCount = sdl_extension_count + additionalExtensionCount;

	err = vkCreateInstance (&instance_create_info, NULL, &vulkan_instance);
	if (err != VK_SUCCESS)
		Sys_Error ("Couldn't create Vulkan instance");

	if (!SDL_Vulkan_CreateSurface (draw_context, vulkan_instance, &vulkan_surface))
		Sys_Error ("Couldn't create Vulkan surface");

	GET_INSTANCE_PROC_ADDR (GetDeviceProcAddr);
	GET_INSTANCE_PROC_ADDR (GetPhysicalDeviceSurfaceSupportKHR);
	GET_INSTANCE_PROC_ADDR (GetPhysicalDeviceSurfaceCapabilitiesKHR);
	GET_INSTANCE_PROC_ADDR (GetPhysicalDeviceSurfaceFormatsKHR);
	GET_INSTANCE_PROC_ADDR (GetPhysicalDeviceSurfacePresentModesKHR);
	GET_INSTANCE_PROC_ADDR (GetSwapchainImagesKHR);

	if (vulkan_globals.get_surface_capabilities_2)
		GET_INSTANCE_PROC_ADDR (GetPhysicalDeviceSurfaceCapabilities2KHR);

	Con_Printf ("Instance extensions:\n");
	for (i = 0; i < (sdl_extension_count + additionalExtensionCount); ++i)
		Con_Printf (" %s\n", instance_extensions[i]);
	Con_Printf ("\n");

#ifdef _DEBUG
	if (vulkan_globals.validation)
	{
		Con_Printf ("Creating debug report callback\n");
		GET_INSTANCE_PROC_ADDR (CreateDebugUtilsMessengerEXT);
		if (fpCreateDebugUtilsMessengerEXT)
		{
			VkDebugUtilsMessengerCreateInfoEXT debug_utils_messenger_create_info;
			memset (&debug_utils_messenger_create_info, 0, sizeof (debug_utils_messenger_create_info));
			debug_utils_messenger_create_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
			debug_utils_messenger_create_info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
			debug_utils_messenger_create_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT;
			debug_utils_messenger_create_info.pfnUserCallback = DebugMessageCallback;

			err = fpCreateDebugUtilsMessengerEXT (vulkan_instance, &debug_utils_messenger_create_info, NULL, &debug_utils_messenger);
			if (err != VK_SUCCESS)
				Sys_Error ("Could not create debug report callback");
		}
	}
#endif

	Mem_Free ((void *)instance_extensions);
}

#if defined(VK_KHR_driver_properties)
enum
{
	DRIVER_ID_AMD_PROPRIETARY = 1,
	DRIVER_ID_AMD_OPEN_SOURCE = 2,
	DRIVER_ID_MESA_RADV = 3,
	DRIVER_ID_NVIDIA_PROPRIETARY = 4,
	DRIVER_ID_INTEL_PROPRIETARY_WINDOWS = 5,
	DRIVER_ID_INTEL_OPEN_SOURCE_MESA = 6,
	DRIVER_ID_IMAGINATION_PROPRIETARY = 7,
	DRIVER_ID_QUALCOMM_PROPRIETARY = 8,
	DRIVER_ID_ARM_PROPRIETARY = 9,
	DRIVER_ID_GOOGLE_SWIFTSHADER = 10,
	DRIVER_ID_GGP_PROPRIETARY = 11,
	DRIVER_ID_BROADCOM_PROPRIETARY = 12,
	DRIVER_ID_MESA_LLVMPIPE = 13,
	DRIVER_ID_MOLTENVK = 14,
	DRIVER_ID_COREAVI_PROPRIETARY = 15,
	DRIVER_ID_JUICE_PROPRIETARY = 16,
	DRIVER_ID_VERISILICON_PROPRIETARY = 17,
	DRIVER_ID_MESA_TURNIP = 18,
	DRIVER_ID_MESA_V3DV = 19,
	DRIVER_ID_MESA_PANVK = 20,
	DRIVER_ID_SAMSUNG_PROPRIETARY = 21,
	DRIVER_ID_MESA_VENUS = 22,
};

/*
===============
GetDeviceVendorFromDriverProperties
===============
*/
static const char *GetDeviceVendorFromDriverProperties (VkPhysicalDeviceDriverProperties *driver_properties)
{
	switch ((int)driver_properties->driverID)
	{
	case DRIVER_ID_AMD_PROPRIETARY:
	case DRIVER_ID_AMD_OPEN_SOURCE:
	case DRIVER_ID_MESA_RADV:
		return "AMD";
	case DRIVER_ID_NVIDIA_PROPRIETARY:
		return "NVIDIA";
	case DRIVER_ID_INTEL_PROPRIETARY_WINDOWS:
	case DRIVER_ID_INTEL_OPEN_SOURCE_MESA:
		return "Intel";
	case DRIVER_ID_IMAGINATION_PROPRIETARY:
		return "ImgTec";
	case DRIVER_ID_QUALCOMM_PROPRIETARY:
	case DRIVER_ID_MESA_TURNIP:
		return "Qualcomm";
	case DRIVER_ID_ARM_PROPRIETARY:
	case DRIVER_ID_MESA_PANVK:
		return "ARM";
	case DRIVER_ID_GOOGLE_SWIFTSHADER:
	case DRIVER_ID_GGP_PROPRIETARY:
		return "Google";
	case DRIVER_ID_BROADCOM_PROPRIETARY:
		return "Broadcom";
	case DRIVER_ID_MESA_V3DV:
		return "Raspberry Pi";
	case DRIVER_ID_MESA_LLVMPIPE:
	case DRIVER_ID_MESA_VENUS:
		return "MESA";
	case DRIVER_ID_MOLTENVK:
		return "MoltenVK";
	case DRIVER_ID_SAMSUNG_PROPRIETARY:
		return "Samsung";
	default:
		return NULL;
	}
}
#endif

/*
===============
GetDeviceVendorFromDeviceProperties
===============
*/
static const char *GetDeviceVendorFromDeviceProperties (void)
{
	switch (vulkan_globals.device_properties.vendorID)
	{
	case 0x8086:
		return "Intel";
	case 0x10DE:
		return "NVIDIA";
	case 0x1002:
		return "AMD";
	case 0x1010:
		return "ImgTec";
	case 0x13B5:
		return "ARM";
	case 0x5143:
		return "Qualcomm";
	}

	return NULL;
}

/*
===============
GL_InitDevice
===============
*/
static void GL_InitDevice (void)
{
	VkResult err;
	uint32_t i;
	int		 argIndex;
	int		 deviceIndex = 0;

#if defined(VK_EXT_subgroup_size_control)
	qboolean subgroup_size_control = false;
#endif

	uint32_t physical_device_count;
	err = vkEnumeratePhysicalDevices (vulkan_instance, &physical_device_count, NULL);
	if (err != VK_SUCCESS || physical_device_count == 0)
		Sys_Error ("Couldn't find any Vulkan devices");

	argIndex = COM_CheckParm ("-device");
	if (argIndex && argIndex < com_argc - 1)
	{
		const char *deviceNum = com_argv[argIndex + 1];
		deviceIndex = CLAMP (0, atoi (deviceNum) - 1, (int)physical_device_count - 1);
	}

	VkPhysicalDevice *physical_devices = (VkPhysicalDevice *)Mem_Alloc (sizeof (VkPhysicalDevice) * physical_device_count);
	err = vkEnumeratePhysicalDevices (vulkan_instance, &physical_device_count, physical_devices);
	vulkan_physical_device = physical_devices[deviceIndex];
	Mem_Free (physical_devices);

	qboolean found_swapchain_extension = false;
	vulkan_globals.dedicated_allocation = false;
	vulkan_globals.full_screen_exclusive = false;
	vulkan_globals.swap_chain_full_screen_acquired = false;
	vulkan_globals.screen_effects_sops = false;

	vkGetPhysicalDeviceMemoryProperties (vulkan_physical_device, &vulkan_globals.memory_properties);
	vkGetPhysicalDeviceProperties (vulkan_physical_device, &vulkan_globals.device_properties);

#if defined(VK_KHR_driver_properties)
	qboolean driver_properties_available = false;
#endif
	uint32_t device_extension_count;
	err = vkEnumerateDeviceExtensionProperties (vulkan_physical_device, NULL, &device_extension_count, NULL);

	if (err == VK_SUCCESS || device_extension_count > 0)
	{
		VkExtensionProperties *device_extensions = (VkExtensionProperties *)Mem_Alloc (sizeof (VkExtensionProperties) * device_extension_count);
		err = vkEnumerateDeviceExtensionProperties (vulkan_physical_device, NULL, &device_extension_count, device_extensions);

		for (i = 0; i < device_extension_count; ++i)
		{
			if (strcmp (VK_KHR_SWAPCHAIN_EXTENSION_NAME, device_extensions[i].extensionName) == 0)
				found_swapchain_extension = true;
			if (strcmp (VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME, device_extensions[i].extensionName) == 0)
				vulkan_globals.dedicated_allocation = true;
#if defined(VK_KHR_driver_properties)
			if (vulkan_globals.get_physical_device_properties_2 && strcmp (VK_KHR_DRIVER_PROPERTIES_EXTENSION_NAME, device_extensions[i].extensionName) == 0)
				driver_properties_available = true;
#endif
#if defined(VK_EXT_subgroup_size_control)
			if (strcmp (VK_EXT_SUBGROUP_SIZE_CONTROL_EXTENSION_NAME, device_extensions[i].extensionName) == 0)
				subgroup_size_control = true;
#endif
#if defined(VK_EXT_full_screen_exclusive)
			if (strcmp (VK_EXT_FULL_SCREEN_EXCLUSIVE_EXTENSION_NAME, device_extensions[i].extensionName) == 0)
				vulkan_globals.full_screen_exclusive = true;
#endif
		}

		Mem_Free (device_extensions);
	}

	const char *vendor = NULL;
#if defined(VK_KHR_driver_properties)
	VkPhysicalDeviceDriverProperties driver_properties;
	if (driver_properties_available)
	{
		memset (&driver_properties, 0, sizeof (driver_properties));
		driver_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;

		VkPhysicalDeviceProperties2 physical_device_properties_2;
		memset (&physical_device_properties_2, 0, sizeof (physical_device_properties_2));
		physical_device_properties_2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
		physical_device_properties_2.pNext = &driver_properties;
		vkGetPhysicalDeviceProperties2 (vulkan_physical_device, &physical_device_properties_2);

		vendor = GetDeviceVendorFromDriverProperties (&driver_properties);
	}
#endif

	if (!vendor)
		vendor = GetDeviceVendorFromDeviceProperties ();

	if (vendor)
		Con_Printf ("Vendor: %s\n", vendor);
	else
		Con_Printf ("Vendor: Unknown (0x%x)\n", vulkan_globals.device_properties.vendorID);

	Con_Printf ("Device: %s\n", vulkan_globals.device_properties.deviceName);

#if defined(VK_KHR_driver_properties)
	if (driver_properties_available)
		Con_Printf ("Driver: %s %s\n", driver_properties.driverName, driver_properties.driverInfo);
#endif

	if (!found_swapchain_extension)
		Sys_Error ("Couldn't find %s extension", VK_KHR_SWAPCHAIN_EXTENSION_NAME);

	qboolean found_graphics_queue = false;

	uint32_t vulkan_queue_count;
	vkGetPhysicalDeviceQueueFamilyProperties (vulkan_physical_device, &vulkan_queue_count, NULL);
	if (vulkan_queue_count == 0)
	{
		Sys_Error ("Couldn't find any Vulkan queues");
	}

	VkQueueFamilyProperties *queue_family_properties = (VkQueueFamilyProperties *)Mem_Alloc (vulkan_queue_count * sizeof (VkQueueFamilyProperties));
	vkGetPhysicalDeviceQueueFamilyProperties (vulkan_physical_device, &vulkan_queue_count, queue_family_properties);

	// Iterate over each queue to learn whether it supports presenting:
	VkBool32 *queue_supports_present = (VkBool32 *)Mem_Alloc (vulkan_queue_count * sizeof (VkBool32));
	for (i = 0; i < vulkan_queue_count; ++i)
		fpGetPhysicalDeviceSurfaceSupportKHR (vulkan_physical_device, i, vulkan_surface, &queue_supports_present[i]);

	for (i = 0; i < vulkan_queue_count; ++i)
	{
		if (((queue_family_properties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) && queue_supports_present[i])
		{
			found_graphics_queue = true;
			vulkan_globals.gfx_queue_family_index = i;
			break;
		}
	}

	Mem_Free (queue_supports_present);
	Mem_Free (queue_family_properties);

	if (!found_graphics_queue)
		Sys_Error ("Couldn't find graphics queue");

	float					queue_priorities[] = {0.0};
	VkDeviceQueueCreateInfo queue_create_info;
	memset (&queue_create_info, 0, sizeof (queue_create_info));
	queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	queue_create_info.queueFamilyIndex = vulkan_globals.gfx_queue_family_index;
	queue_create_info.queueCount = 1;
	queue_create_info.pQueuePriorities = queue_priorities;

#if defined(VK_EXT_subgroup_size_control)
	VkPhysicalDeviceSubgroupProperties physical_device_subgroup_properties;
	memset (&physical_device_subgroup_properties, 0, sizeof (physical_device_subgroup_properties));
	VkPhysicalDeviceSubgroupSizeControlPropertiesEXT physical_device_subgroup_size_control_properties;
	memset (&physical_device_subgroup_size_control_properties, 0, sizeof (physical_device_subgroup_size_control_properties));
	VkPhysicalDeviceSubgroupSizeControlFeaturesEXT subgroup_size_control_features;
	memset (&subgroup_size_control_features, 0, sizeof (subgroup_size_control_features));
	if (vulkan_globals.vulkan_1_1_available && subgroup_size_control)
	{
		memset (&physical_device_subgroup_size_control_properties, 0, sizeof (physical_device_subgroup_size_control_properties));
		physical_device_subgroup_size_control_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_PROPERTIES_EXT;
		memset (&physical_device_subgroup_properties, 0, sizeof (physical_device_subgroup_properties));
		physical_device_subgroup_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES;
		physical_device_subgroup_properties.pNext = &physical_device_subgroup_size_control_properties;
		VkPhysicalDeviceProperties2 physical_device_properties_2;
		memset (&physical_device_properties_2, 0, sizeof (physical_device_properties_2));
		physical_device_properties_2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
		physical_device_properties_2.pNext = &physical_device_subgroup_properties;
		vkGetPhysicalDeviceProperties2 (vulkan_physical_device, &physical_device_properties_2);

		memset (&subgroup_size_control_features, 0, sizeof (subgroup_size_control_features));
		subgroup_size_control_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_FEATURES_EXT;
		VkPhysicalDeviceFeatures2 physical_device_features_2;
		memset (&physical_device_features_2, 0, sizeof (physical_device_features_2));
		physical_device_features_2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
		physical_device_features_2.pNext = &subgroup_size_control_features;
		vkGetPhysicalDeviceFeatures2 (vulkan_physical_device, &physical_device_features_2);

		vulkan_physical_device_features = physical_device_features_2.features;
	}
	else
#endif
		vkGetPhysicalDeviceFeatures (vulkan_physical_device, &vulkan_physical_device_features);

#if defined(VK_EXT_subgroup_size_control)
	vulkan_globals.screen_effects_sops =
		vulkan_globals.vulkan_1_1_available && subgroup_size_control && subgroup_size_control_features.subgroupSizeControl &&
		subgroup_size_control_features.computeFullSubgroups && ((physical_device_subgroup_properties.supportedStages & VK_SHADER_STAGE_COMPUTE_BIT) != 0) &&
		((physical_device_subgroup_properties.supportedOperations & VK_SUBGROUP_FEATURE_SHUFFLE_BIT) != 0)
		// Shader only supports subgroup sizes from 4 to 64. 128 can't be supported because Vulkan spec states that workgroup size
		// in x dimension must be a multiple of the subgroup size for VK_PIPELINE_SHADER_STAGE_CREATE_REQUIRE_FULL_SUBGROUPS_BIT_EXT.
		&& (physical_device_subgroup_size_control_properties.minSubgroupSize >= 4) && (physical_device_subgroup_size_control_properties.maxSubgroupSize <= 64);

	if (vulkan_globals.screen_effects_sops)
		Con_Printf ("Using subgroup operations\n");
#endif

	const char *device_extensions[6] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
	uint32_t	numEnabledExtensions = 1;
	if (vulkan_globals.dedicated_allocation)
	{
		device_extensions[numEnabledExtensions++] = VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME;
		device_extensions[numEnabledExtensions++] = VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME;
	}
#if defined(VK_EXT_subgroup_size_control)
	if (vulkan_globals.screen_effects_sops)
		device_extensions[numEnabledExtensions++] = VK_EXT_SUBGROUP_SIZE_CONTROL_EXTENSION_NAME;
#endif
#if defined(VK_EXT_full_screen_exclusive)
	if (vulkan_globals.full_screen_exclusive)
	{
		device_extensions[numEnabledExtensions++] = VK_EXT_FULL_SCREEN_EXCLUSIVE_EXTENSION_NAME;
	}
#endif

	const VkBool32 extended_format_support = vulkan_physical_device_features.shaderStorageImageExtendedFormats;
	const VkBool32 sampler_anisotropic = vulkan_physical_device_features.samplerAnisotropy;

	VkPhysicalDeviceFeatures device_features;
	memset (&device_features, 0, sizeof (device_features));
	device_features.shaderStorageImageExtendedFormats = extended_format_support;
	device_features.samplerAnisotropy = sampler_anisotropic;
	device_features.sampleRateShading = vulkan_physical_device_features.sampleRateShading;
	device_features.fillModeNonSolid = vulkan_physical_device_features.fillModeNonSolid;

	vulkan_globals.non_solid_fill = (device_features.fillModeNonSolid == VK_TRUE) ? true : false;

	VkDeviceCreateInfo device_create_info;
	memset (&device_create_info, 0, sizeof (device_create_info));
	device_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
#if defined(VK_EXT_subgroup_size_control)
	device_create_info.pNext = vulkan_globals.screen_effects_sops ? &subgroup_size_control_features : NULL;
#endif
	device_create_info.queueCreateInfoCount = 1;
	device_create_info.pQueueCreateInfos = &queue_create_info;
	device_create_info.enabledExtensionCount = numEnabledExtensions;
	device_create_info.ppEnabledExtensionNames = device_extensions;
	device_create_info.pEnabledFeatures = &device_features;

	err = vkCreateDevice (vulkan_physical_device, &device_create_info, NULL, &vulkan_globals.device);
	if (err != VK_SUCCESS)
		Sys_Error ("Couldn't create Vulkan device");

	GET_DEVICE_PROC_ADDR (CreateSwapchainKHR);
	GET_DEVICE_PROC_ADDR (DestroySwapchainKHR);
	GET_DEVICE_PROC_ADDR (GetSwapchainImagesKHR);
	GET_DEVICE_PROC_ADDR (AcquireNextImageKHR);
	GET_DEVICE_PROC_ADDR (QueuePresentKHR);

	Con_Printf ("Device extensions:\n");
	for (i = 0; i < numEnabledExtensions; ++i)
		Con_Printf (" %s\n", device_extensions[i]);

#if defined(VK_EXT_full_screen_exclusive)
	if (vulkan_globals.full_screen_exclusive)
	{
		GET_DEVICE_PROC_ADDR (AcquireFullScreenExclusiveModeEXT);
		GET_DEVICE_PROC_ADDR (ReleaseFullScreenExclusiveModeEXT);
	}
#endif
#ifdef _DEBUG
	if (vulkan_globals.debug_utils)
	{
		GET_INSTANCE_PROC_ADDR (SetDebugUtilsObjectNameEXT);
		GET_GLOBAL_INSTANCE_PROC_ADDR (vk_cmd_begin_debug_utils_label, vkCmdBeginDebugUtilsLabelEXT);
		GET_GLOBAL_INSTANCE_PROC_ADDR (vk_cmd_end_debug_utils_label, vkCmdEndDebugUtilsLabelEXT);
	}
#endif

	vkGetDeviceQueue (vulkan_globals.device, vulkan_globals.gfx_queue_family_index, 0, &vulkan_globals.queue);

	VkFormatProperties format_properties;

	// Find color buffer format
	vulkan_globals.color_format = VK_FORMAT_R8G8B8A8_UNORM;

	if (extended_format_support == VK_TRUE)
	{
		vkGetPhysicalDeviceFormatProperties (vulkan_physical_device, VK_FORMAT_A2B10G10R10_UNORM_PACK32, &format_properties);
		qboolean a2_b10_g10_r10_support = (format_properties.optimalTilingFeatures & REQUIRED_COLOR_BUFFER_FEATURES) == REQUIRED_COLOR_BUFFER_FEATURES;

		if (a2_b10_g10_r10_support)
		{
			Con_Printf ("Using A2B10G10R10 color buffer format\n");
			vulkan_globals.color_format = VK_FORMAT_A2B10G10R10_UNORM_PACK32;
		}
	}

	// Find depth format
	vkGetPhysicalDeviceFormatProperties (vulkan_physical_device, VK_FORMAT_D24_UNORM_S8_UINT, &format_properties);
	qboolean x8_d24_support = (format_properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0;
	vkGetPhysicalDeviceFormatProperties (vulkan_physical_device, VK_FORMAT_D32_SFLOAT_S8_UINT, &format_properties);
	qboolean d32_support = (format_properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0;

	vulkan_globals.depth_format = VK_FORMAT_UNDEFINED;
	if (d32_support)
	{
		Con_Printf ("Using D32_S8 depth buffer format\n");
		vulkan_globals.depth_format = VK_FORMAT_D32_SFLOAT_S8_UINT;
	}
	else if (x8_d24_support)
	{
		Con_Printf ("Using D24_S8 depth buffer format\n");
		vulkan_globals.depth_format = VK_FORMAT_D24_UNORM_S8_UINT;
	}
	else
	{
		// This cannot happen with a compliant Vulkan driver. The spec requires support for one of the formats.
		Sys_Error ("Cannot find VK_FORMAT_D24_UNORM_S8_UINT or VK_FORMAT_D32_SFLOAT_S8_UINT depth buffer format");
	}

	Con_Printf ("\n");

	GET_GLOBAL_DEVICE_PROC_ADDR (vk_cmd_bind_pipeline, vkCmdBindPipeline);
	GET_GLOBAL_DEVICE_PROC_ADDR (vk_cmd_push_constants, vkCmdPushConstants);
	GET_GLOBAL_DEVICE_PROC_ADDR (vk_cmd_bind_descriptor_sets, vkCmdBindDescriptorSets);
	GET_GLOBAL_DEVICE_PROC_ADDR (vk_cmd_bind_index_buffer, vkCmdBindIndexBuffer);
	GET_GLOBAL_DEVICE_PROC_ADDR (vk_cmd_bind_vertex_buffers, vkCmdBindVertexBuffers);
	GET_GLOBAL_DEVICE_PROC_ADDR (vk_cmd_draw, vkCmdDraw);
	GET_GLOBAL_DEVICE_PROC_ADDR (vk_cmd_draw_indexed, vkCmdDrawIndexed);
	GET_GLOBAL_DEVICE_PROC_ADDR (vk_cmd_draw_indexed_indirect, vkCmdDrawIndexedIndirect);
	GET_GLOBAL_DEVICE_PROC_ADDR (vk_cmd_pipeline_barrier, vkCmdPipelineBarrier);
	GET_GLOBAL_DEVICE_PROC_ADDR (vk_cmd_copy_buffer_to_image, vkCmdCopyBufferToImage);
}

/*
===============
GL_InitCommandBuffers
===============
*/
static void GL_InitCommandBuffers (void)
{
	Con_Printf ("Creating command buffers\n");

	VkResult err;

	{
		VkCommandPoolCreateInfo command_pool_create_info;
		memset (&command_pool_create_info, 0, sizeof (command_pool_create_info));
		command_pool_create_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		command_pool_create_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
		command_pool_create_info.queueFamilyIndex = vulkan_globals.gfx_queue_family_index;
		err = vkCreateCommandPool (vulkan_globals.device, &command_pool_create_info, NULL, &transient_command_pool);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateCommandPool failed");
	}

	VkCommandPoolCreateInfo command_pool_create_info;
	memset (&command_pool_create_info, 0, sizeof (command_pool_create_info));
	command_pool_create_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	command_pool_create_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	command_pool_create_info.queueFamilyIndex = vulkan_globals.gfx_queue_family_index;

	{
		err = vkCreateCommandPool (vulkan_globals.device, &command_pool_create_info, NULL, &primary_command_pool);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateCommandPool failed");

		VkCommandBufferAllocateInfo command_buffer_allocate_info;
		memset (&command_buffer_allocate_info, 0, sizeof (command_buffer_allocate_info));
		command_buffer_allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		command_buffer_allocate_info.commandPool = primary_command_pool;
		command_buffer_allocate_info.commandBufferCount = DOUBLE_BUFFERED;

		err = vkAllocateCommandBuffers (vulkan_globals.device, &command_buffer_allocate_info, primary_command_buffers);
		if (err != VK_SUCCESS)
			Sys_Error ("vkAllocateCommandBuffers failed");
	}

	for (int cbx_index = 0; cbx_index < CBX_NUM; ++cbx_index)
	{
		err = vkCreateCommandPool (vulkan_globals.device, &command_pool_create_info, NULL, &secondary_command_pools[cbx_index]);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateCommandPool failed");

		VkCommandBufferAllocateInfo command_buffer_allocate_info;
		memset (&command_buffer_allocate_info, 0, sizeof (command_buffer_allocate_info));
		command_buffer_allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		command_buffer_allocate_info.commandPool = secondary_command_pools[cbx_index];
		command_buffer_allocate_info.commandBufferCount = DOUBLE_BUFFERED;
		command_buffer_allocate_info.level = VK_COMMAND_BUFFER_LEVEL_SECONDARY;

		err = vkAllocateCommandBuffers (vulkan_globals.device, &command_buffer_allocate_info, secondary_command_buffers[cbx_index]);
		if (err != VK_SUCCESS)
			Sys_Error ("vkAllocateCommandBuffers failed");
	}

	VkFenceCreateInfo fence_create_info;
	memset (&fence_create_info, 0, sizeof (fence_create_info));
	fence_create_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

	for (int i = 0; i < DOUBLE_BUFFERED; ++i)
	{
		err = vkCreateFence (vulkan_globals.device, &fence_create_info, NULL, &command_buffer_fences[i]);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateFence failed");

		VkSemaphoreCreateInfo semaphore_create_info;
		memset (&semaphore_create_info, 0, sizeof (semaphore_create_info));
		semaphore_create_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
		err = vkCreateSemaphore (vulkan_globals.device, &semaphore_create_info, NULL, &draw_complete_semaphores[i]);
	}
}

/*
====================
GL_CreateRenderPasses
====================
*/
static void GL_CreateRenderPasses ()
{
	Sys_Printf ("Creating render passes\n");

	VkResult err;

	{
		// Main render pass
		VkAttachmentDescription attachment_descriptions[3];
		memset (&attachment_descriptions, 0, sizeof (attachment_descriptions));

		const qboolean resolve = (vulkan_globals.sample_count != VK_SAMPLE_COUNT_1_BIT);

		attachment_descriptions[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachment_descriptions[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		attachment_descriptions[0].samples = VK_SAMPLE_COUNT_1_BIT;
		attachment_descriptions[0].format = vulkan_globals.color_format;
		attachment_descriptions[0].loadOp = resolve ? VK_ATTACHMENT_LOAD_OP_DONT_CARE : VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachment_descriptions[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;

		attachment_descriptions[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachment_descriptions[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		attachment_descriptions[1].samples = vulkan_globals.sample_count;
		attachment_descriptions[1].format = vulkan_globals.depth_format;
		attachment_descriptions[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachment_descriptions[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachment_descriptions[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachment_descriptions[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

		attachment_descriptions[2].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachment_descriptions[2].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		attachment_descriptions[2].samples = vulkan_globals.sample_count;
		attachment_descriptions[2].format = vulkan_globals.color_format;
		attachment_descriptions[2].loadOp = resolve ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachment_descriptions[2].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

		VkAttachmentReference scene_color_attachment_reference;
		scene_color_attachment_reference.attachment = resolve ? 2 : 0;
		scene_color_attachment_reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		VkAttachmentReference depth_attachment_reference;
		depth_attachment_reference.attachment = 1;
		depth_attachment_reference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		VkAttachmentReference resolve_attachment_reference;
		resolve_attachment_reference.attachment = 0;
		resolve_attachment_reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		VkSubpassDescription subpass_description;
		memset (&subpass_description, 0, sizeof (subpass_description));

		subpass_description.colorAttachmentCount = 1;
		subpass_description.pColorAttachments = &scene_color_attachment_reference;
		subpass_description.pDepthStencilAttachment = &depth_attachment_reference;
		subpass_description.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		if (resolve)
			subpass_description.pResolveAttachments = &resolve_attachment_reference;

		VkRenderPassCreateInfo render_pass_create_info;
		memset (&render_pass_create_info, 0, sizeof (render_pass_create_info));
		render_pass_create_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		render_pass_create_info.attachmentCount = resolve ? 3 : 2;
		render_pass_create_info.pAttachments = attachment_descriptions;
		render_pass_create_info.subpassCount = 1;
		render_pass_create_info.pSubpasses = &subpass_description;

		for (int cbx_index = CBX_WORLD_0; cbx_index <= CBX_VIEW_MODEL; ++cbx_index)
			assert (vulkan_globals.secondary_cb_contexts[cbx_index].render_pass == VK_NULL_HANDLE);

		err = vkCreateRenderPass (vulkan_globals.device, &render_pass_create_info, NULL, &vulkan_globals.main_render_pass[0]);
		if (err != VK_SUCCESS)
			Sys_Error ("Couldn't create Vulkan render pass");
		GL_SetObjectName ((uint64_t)vulkan_globals.main_render_pass[0], VK_OBJECT_TYPE_RENDER_PASS, "main");

		attachment_descriptions[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		err = vkCreateRenderPass (vulkan_globals.device, &render_pass_create_info, NULL, &vulkan_globals.main_render_pass[1]);
		if (err != VK_SUCCESS)
			Sys_Error ("Couldn't create Vulkan render pass");
		GL_SetObjectName ((uint64_t)vulkan_globals.main_render_pass[1], VK_OBJECT_TYPE_RENDER_PASS, "main_no_stencil");

		for (int cbx_index = CBX_WORLD_0; cbx_index <= CBX_VIEW_MODEL; ++cbx_index)
		{
			cb_context_t *cbx = &vulkan_globals.secondary_cb_contexts[cbx_index];
			cbx->render_pass = vulkan_globals.main_render_pass[0];
			cbx->render_pass_index = 0;
			cbx->subpass = 0;
		}
	}

	{
		// UI Render Pass
		VkAttachmentDescription attachment_descriptions[2];
		memset (&attachment_descriptions, 0, sizeof (attachment_descriptions));

		attachment_descriptions[0].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		attachment_descriptions[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		attachment_descriptions[0].samples = VK_SAMPLE_COUNT_1_BIT;
		attachment_descriptions[0].format = vulkan_globals.color_format;
		attachment_descriptions[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
		attachment_descriptions[0].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

		attachment_descriptions[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachment_descriptions[1].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		attachment_descriptions[1].samples = VK_SAMPLE_COUNT_1_BIT;
		attachment_descriptions[1].format = vulkan_globals.swap_chain_format;
		attachment_descriptions[1].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachment_descriptions[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;

		VkAttachmentReference color_input_attachment_reference;
		color_input_attachment_reference.attachment = 0;
		color_input_attachment_reference.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		VkAttachmentReference ui_color_attachment_reference;
		ui_color_attachment_reference.attachment = 0;
		ui_color_attachment_reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		VkAttachmentReference swap_chain_attachment_reference;
		swap_chain_attachment_reference.attachment = 1;
		swap_chain_attachment_reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		VkSubpassDescription subpass_descriptions[2];
		memset (&subpass_descriptions, 0, sizeof (subpass_descriptions));
		subpass_descriptions[0].colorAttachmentCount = 1;
		subpass_descriptions[0].pColorAttachments = &ui_color_attachment_reference;
		subpass_descriptions[0].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;

		subpass_descriptions[1].colorAttachmentCount = 1;
		subpass_descriptions[1].pColorAttachments = &swap_chain_attachment_reference;
		subpass_descriptions[1].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass_descriptions[1].inputAttachmentCount = 1;
		subpass_descriptions[1].pInputAttachments = &color_input_attachment_reference;

		VkSubpassDependency subpass_dependencies[1];
		subpass_dependencies[0].srcSubpass = 0;
		subpass_dependencies[0].dstSubpass = 1;
		subpass_dependencies[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		subpass_dependencies[0].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		subpass_dependencies[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		subpass_dependencies[0].dstAccessMask = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
		subpass_dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		VkRenderPassCreateInfo render_pass_create_info;
		memset (&render_pass_create_info, 0, sizeof (render_pass_create_info));
		render_pass_create_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		render_pass_create_info.attachmentCount = 2;
		render_pass_create_info.pAttachments = attachment_descriptions;
		render_pass_create_info.subpassCount = 2;
		render_pass_create_info.pSubpasses = subpass_descriptions;
		render_pass_create_info.dependencyCount = 1;
		render_pass_create_info.pDependencies = subpass_dependencies;

		cb_context_t *gui_cbx = &vulkan_globals.secondary_cb_contexts[CBX_GUI];
		cb_context_t *post_process_cbx = &vulkan_globals.secondary_cb_contexts[CBX_POST_PROCESS];

		assert (gui_cbx->render_pass == VK_NULL_HANDLE);
		assert (post_process_cbx->render_pass == VK_NULL_HANDLE);

		VkRenderPass render_pass;
		err = vkCreateRenderPass (vulkan_globals.device, &render_pass_create_info, NULL, &render_pass);
		if (err != VK_SUCCESS)
			Sys_Error ("Couldn't create Vulkan render pass");
		GL_SetObjectName ((uint64_t)render_pass, VK_OBJECT_TYPE_RENDER_PASS, "ui");

		gui_cbx->render_pass = render_pass;
		gui_cbx->render_pass_index = 1;
		gui_cbx->subpass = 0;
		post_process_cbx->render_pass = render_pass;
		post_process_cbx->render_pass_index = 1;
		post_process_cbx->subpass = 1;
	}

	if (vulkan_globals.warp_render_pass == VK_NULL_HANDLE)
	{
		VkAttachmentDescription attachment_description;
		memset (&attachment_description, 0, sizeof (attachment_description));

		// Warp rendering
		attachment_description.format = VK_FORMAT_R8G8B8A8_UNORM;
		attachment_description.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachment_description.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachment_description.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachment_description.finalLayout = VK_IMAGE_LAYOUT_GENERAL;
		attachment_description.samples = VK_SAMPLE_COUNT_1_BIT;

		VkAttachmentReference scene_color_attachment_reference;
		scene_color_attachment_reference.attachment = 0;
		scene_color_attachment_reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		VkSubpassDescription subpass_description;
		memset (&subpass_description, 0, sizeof (subpass_description));
		subpass_description.colorAttachmentCount = 1;
		subpass_description.pColorAttachments = &scene_color_attachment_reference;
		subpass_description.pDepthStencilAttachment = NULL;
		subpass_description.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;

		VkRenderPassCreateInfo render_pass_create_info;
		memset (&render_pass_create_info, 0, sizeof (render_pass_create_info));
		render_pass_create_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		render_pass_create_info.pAttachments = &attachment_description;
		render_pass_create_info.subpassCount = 1;
		render_pass_create_info.pSubpasses = &subpass_description;
		render_pass_create_info.attachmentCount = 1;
		render_pass_create_info.dependencyCount = 0;
		render_pass_create_info.pDependencies = NULL;

		err = vkCreateRenderPass (vulkan_globals.device, &render_pass_create_info, NULL, &vulkan_globals.warp_render_pass);
		if (err != VK_SUCCESS)
			Sys_Error ("Couldn't create Vulkan render pass");

		GL_SetObjectName ((uint64_t)vulkan_globals.warp_render_pass, VK_OBJECT_TYPE_RENDER_PASS, "warp");
	}
}

/*
===============
GL_CreateDepthBuffer
===============
*/
static void GL_CreateDepthBuffer (void)
{
	Sys_Printf ("Creating depth buffer\n");

	if (depth_buffer != VK_NULL_HANDLE)
		return;

	VkResult err;

	VkImageCreateInfo image_create_info;
	memset (&image_create_info, 0, sizeof (image_create_info));
	image_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	image_create_info.pNext = NULL;
	image_create_info.imageType = VK_IMAGE_TYPE_2D;
	image_create_info.format = vulkan_globals.depth_format;
	image_create_info.extent.width = vid.width;
	image_create_info.extent.height = vid.height;
	image_create_info.extent.depth = 1;
	image_create_info.mipLevels = 1;
	image_create_info.arrayLayers = 1;
	image_create_info.samples = vulkan_globals.sample_count;
	image_create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
	image_create_info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

	assert (depth_buffer == VK_NULL_HANDLE);
	err = vkCreateImage (vulkan_globals.device, &image_create_info, NULL, &depth_buffer);
	if (err != VK_SUCCESS)
		Sys_Error ("vkCreateImage failed");

	GL_SetObjectName ((uint64_t)depth_buffer, VK_OBJECT_TYPE_IMAGE, "Depth Buffer");

	VkMemoryRequirements memory_requirements;
	vkGetImageMemoryRequirements (vulkan_globals.device, depth_buffer, &memory_requirements);

	VkMemoryDedicatedAllocateInfoKHR dedicated_allocation_info;
	memset (&dedicated_allocation_info, 0, sizeof (dedicated_allocation_info));
	dedicated_allocation_info.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO_KHR;
	dedicated_allocation_info.image = depth_buffer;

	VkMemoryAllocateInfo memory_allocate_info;
	memset (&memory_allocate_info, 0, sizeof (memory_allocate_info));
	memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	memory_allocate_info.allocationSize = memory_requirements.size;
	memory_allocate_info.memoryTypeIndex = GL_MemoryTypeFromProperties (memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);

	if (vulkan_globals.dedicated_allocation)
		memory_allocate_info.pNext = &dedicated_allocation_info;

	assert (depth_buffer_memory.handle == VK_NULL_HANDLE);
	Atomic_IncrementUInt32 (&num_vulkan_misc_allocations);
	R_AllocateVulkanMemory (&depth_buffer_memory, &memory_allocate_info, VULKAN_MEMORY_TYPE_DEVICE);
	GL_SetObjectName ((uint64_t)depth_buffer_memory.handle, VK_OBJECT_TYPE_DEVICE_MEMORY, "Depth Buffer");

	err = vkBindImageMemory (vulkan_globals.device, depth_buffer, depth_buffer_memory.handle, 0);
	if (err != VK_SUCCESS)
		Sys_Error ("vkBindImageMemory failed");

	VkImageViewCreateInfo image_view_create_info;
	memset (&image_view_create_info, 0, sizeof (image_view_create_info));
	image_view_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	image_view_create_info.format = vulkan_globals.depth_format;
	image_view_create_info.image = depth_buffer;
	image_view_create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
	image_view_create_info.subresourceRange.baseMipLevel = 0;
	image_view_create_info.subresourceRange.levelCount = 1;
	image_view_create_info.subresourceRange.baseArrayLayer = 0;
	image_view_create_info.subresourceRange.layerCount = 1;
	image_view_create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
	image_view_create_info.flags = 0;

	assert (depth_buffer_view == VK_NULL_HANDLE);
	err = vkCreateImageView (vulkan_globals.device, &image_view_create_info, NULL, &depth_buffer_view);
	if (err != VK_SUCCESS)
		Sys_Error ("vkCreateImageView failed");

	GL_SetObjectName ((uint64_t)depth_buffer_view, VK_OBJECT_TYPE_IMAGE_VIEW, "Depth Buffer View");
}

/*
===============
GL_CreateColorBuffer
===============
*/
static void GL_CreateColorBuffer (void)
{
	VkResult err;
	int		 i;

	Sys_Printf ("Creating color buffer\n");

	VkImageCreateInfo image_create_info;
	memset (&image_create_info, 0, sizeof (image_create_info));
	image_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	image_create_info.pNext = NULL;
	image_create_info.imageType = VK_IMAGE_TYPE_2D;
	image_create_info.format = vulkan_globals.color_format;
	image_create_info.extent.width = vid.width;
	image_create_info.extent.height = vid.height;
	image_create_info.extent.depth = 1;
	image_create_info.mipLevels = 1;
	image_create_info.arrayLayers = 1;
	image_create_info.samples = VK_SAMPLE_COUNT_1_BIT;
	image_create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
	image_create_info.usage =
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;

	for (i = 0; i < NUM_COLOR_BUFFERS; ++i)
	{
		assert (vulkan_globals.color_buffers[i] == VK_NULL_HANDLE);
		err = vkCreateImage (vulkan_globals.device, &image_create_info, NULL, &vulkan_globals.color_buffers[i]);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateImage failed");

		GL_SetObjectName ((uint64_t)vulkan_globals.color_buffers[i], VK_OBJECT_TYPE_IMAGE, va ("Color Buffer %d", i));

		VkMemoryRequirements memory_requirements;
		vkGetImageMemoryRequirements (vulkan_globals.device, vulkan_globals.color_buffers[i], &memory_requirements);

		VkMemoryDedicatedAllocateInfoKHR dedicated_allocation_info;
		memset (&dedicated_allocation_info, 0, sizeof (dedicated_allocation_info));
		dedicated_allocation_info.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO_KHR;
		dedicated_allocation_info.image = vulkan_globals.color_buffers[i];

		VkMemoryAllocateInfo memory_allocate_info;
		memset (&memory_allocate_info, 0, sizeof (memory_allocate_info));
		memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		memory_allocate_info.allocationSize = memory_requirements.size;
		memory_allocate_info.memoryTypeIndex = GL_MemoryTypeFromProperties (memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);

		if (vulkan_globals.dedicated_allocation)
			memory_allocate_info.pNext = &dedicated_allocation_info;

		assert (color_buffers_memory[i].handle == VK_NULL_HANDLE);
		Atomic_IncrementUInt32 (&num_vulkan_misc_allocations);
		R_AllocateVulkanMemory (&color_buffers_memory[i], &memory_allocate_info, VULKAN_MEMORY_TYPE_DEVICE);
		GL_SetObjectName ((uint64_t)color_buffers_memory[i].handle, VK_OBJECT_TYPE_DEVICE_MEMORY, va ("Color Buffer %d", i));

		err = vkBindImageMemory (vulkan_globals.device, vulkan_globals.color_buffers[i], color_buffers_memory[i].handle, 0);
		if (err != VK_SUCCESS)
			Sys_Error ("vkBindImageMemory failed");

		VkImageViewCreateInfo image_view_create_info;
		memset (&image_view_create_info, 0, sizeof (image_view_create_info));
		image_view_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		image_view_create_info.format = vulkan_globals.color_format;
		image_view_create_info.image = vulkan_globals.color_buffers[i];
		image_view_create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		image_view_create_info.subresourceRange.baseMipLevel = 0;
		image_view_create_info.subresourceRange.levelCount = 1;
		image_view_create_info.subresourceRange.baseArrayLayer = 0;
		image_view_create_info.subresourceRange.layerCount = 1;
		image_view_create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
		image_view_create_info.flags = 0;

		assert (color_buffers_view[i] == VK_NULL_HANDLE);
		err = vkCreateImageView (vulkan_globals.device, &image_view_create_info, NULL, &color_buffers_view[i]);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateImageView failed");

		GL_SetObjectName ((uint64_t)color_buffers_view[i], VK_OBJECT_TYPE_IMAGE_VIEW, va ("Color Buffer View %d", i));
	}

	vulkan_globals.sample_count = VK_SAMPLE_COUNT_1_BIT;
	vulkan_globals.supersampling = false;

	{
		const int fsaa = (int)vid_fsaa.value;

		VkImageFormatProperties image_format_properties;
		vkGetPhysicalDeviceImageFormatProperties (
			vulkan_physical_device, vulkan_globals.color_format, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL, image_create_info.usage, 0,
			&image_format_properties);

		// Workaround: Intel advertises 16 samples but crashes when using it.
		if ((fsaa >= 16) && (image_format_properties.sampleCounts & VK_SAMPLE_COUNT_16_BIT) && (vulkan_globals.device_properties.vendorID != 0x8086))
			vulkan_globals.sample_count = VK_SAMPLE_COUNT_16_BIT;
		else if ((fsaa >= 8) && (image_format_properties.sampleCounts & VK_SAMPLE_COUNT_8_BIT))
			vulkan_globals.sample_count = VK_SAMPLE_COUNT_8_BIT;
		else if ((fsaa >= 4) && (image_format_properties.sampleCounts & VK_SAMPLE_COUNT_4_BIT))
			vulkan_globals.sample_count = VK_SAMPLE_COUNT_4_BIT;
		else if ((fsaa >= 2) && (image_format_properties.sampleCounts & VK_SAMPLE_COUNT_2_BIT))
			vulkan_globals.sample_count = VK_SAMPLE_COUNT_2_BIT;

		switch (vulkan_globals.sample_count)
		{
		case VK_SAMPLE_COUNT_2_BIT:
			Sys_Printf ("2 AA Samples\n");
			break;
		case VK_SAMPLE_COUNT_4_BIT:
			Sys_Printf ("4 AA Samples\n");
			break;
		case VK_SAMPLE_COUNT_8_BIT:
			Sys_Printf ("8 AA Samples\n");
			break;
		case VK_SAMPLE_COUNT_16_BIT:
			Sys_Printf ("16 AA Samples\n");
			break;
		default:
			break;
		}
	}

	if (vulkan_globals.sample_count != VK_SAMPLE_COUNT_1_BIT)
	{
#ifndef __APPLE__ // MoltenVK lies about this
		vulkan_globals.supersampling = (vulkan_physical_device_features.sampleRateShading && vid_fsaamode.value >= 1) ? true : false;
#endif

		if (vulkan_globals.supersampling)
			Sys_Printf ("Supersampling enabled\n");

		image_create_info.samples = vulkan_globals.sample_count;
		image_create_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

		assert (msaa_color_buffer == VK_NULL_HANDLE);
		err = vkCreateImage (vulkan_globals.device, &image_create_info, NULL, &msaa_color_buffer);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateImage failed");

		GL_SetObjectName ((uint64_t)msaa_color_buffer, VK_OBJECT_TYPE_IMAGE, "MSAA Color Buffer");

		VkMemoryRequirements memory_requirements;
		vkGetImageMemoryRequirements (vulkan_globals.device, msaa_color_buffer, &memory_requirements);

		VkMemoryDedicatedAllocateInfoKHR dedicated_allocation_info;
		memset (&dedicated_allocation_info, 0, sizeof (dedicated_allocation_info));
		dedicated_allocation_info.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO_KHR;
		dedicated_allocation_info.image = msaa_color_buffer;

		VkMemoryAllocateInfo memory_allocate_info;
		memset (&memory_allocate_info, 0, sizeof (memory_allocate_info));
		memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		memory_allocate_info.allocationSize = memory_requirements.size;
		memory_allocate_info.memoryTypeIndex = GL_MemoryTypeFromProperties (memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);

		if (vulkan_globals.dedicated_allocation)
			memory_allocate_info.pNext = &dedicated_allocation_info;

		assert (msaa_color_buffer_memory.handle == VK_NULL_HANDLE);
		Atomic_IncrementUInt32 (&num_vulkan_misc_allocations);
		R_AllocateVulkanMemory (&msaa_color_buffer_memory, &memory_allocate_info, VULKAN_MEMORY_TYPE_DEVICE);
		GL_SetObjectName ((uint64_t)msaa_color_buffer_memory.handle, VK_OBJECT_TYPE_DEVICE_MEMORY, "MSAA Color Buffer");

		err = vkBindImageMemory (vulkan_globals.device, msaa_color_buffer, msaa_color_buffer_memory.handle, 0);
		if (err != VK_SUCCESS)
			Sys_Error ("vkBindImageMemory failed");

		VkImageViewCreateInfo image_view_create_info;
		memset (&image_view_create_info, 0, sizeof (image_view_create_info));
		image_view_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		image_view_create_info.format = vulkan_globals.color_format;
		image_view_create_info.image = msaa_color_buffer;
		image_view_create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		image_view_create_info.subresourceRange.baseMipLevel = 0;
		image_view_create_info.subresourceRange.levelCount = 1;
		image_view_create_info.subresourceRange.baseArrayLayer = 0;
		image_view_create_info.subresourceRange.layerCount = 1;
		image_view_create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
		image_view_create_info.flags = 0;

		assert (msaa_color_buffer_view == VK_NULL_HANDLE);
		err = vkCreateImageView (vulkan_globals.device, &image_view_create_info, NULL, &msaa_color_buffer_view);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateImageView failed");
	}
	else
		Sys_Printf ("AA disabled\n");
}

/*
===============
GL_UpdateDescriptorSets
===============
*/
static void GL_UpdateDescriptorSets (void)
{
	assert (postprocess_descriptor_set == VK_NULL_HANDLE);
	postprocess_descriptor_set = R_AllocateDescriptorSet (&vulkan_globals.input_attachment_set_layout);

	VkDescriptorImageInfo image_info;
	memset (&image_info, 0, sizeof (image_info));
	image_info.imageView = color_buffers_view[0];
	image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	VkWriteDescriptorSet input_attachment_write;
	memset (&input_attachment_write, 0, sizeof (input_attachment_write));
	input_attachment_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	input_attachment_write.dstBinding = 0;
	input_attachment_write.dstArrayElement = 0;
	input_attachment_write.descriptorCount = 1;
	input_attachment_write.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
	input_attachment_write.dstSet = postprocess_descriptor_set;
	input_attachment_write.pImageInfo = &image_info;
	vkUpdateDescriptorSets (vulkan_globals.device, 1, &input_attachment_write, 0, NULL);

	assert (vulkan_globals.screen_effects_desc_set == VK_NULL_HANDLE);
	vulkan_globals.screen_effects_desc_set = R_AllocateDescriptorSet (&vulkan_globals.screen_effects_set_layout);

	VkDescriptorImageInfo input_image_info;
	memset (&input_image_info, 0, sizeof (input_image_info));
	input_image_info.imageView = color_buffers_view[1];
	input_image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	input_image_info.sampler = vulkan_globals.linear_sampler;

	VkDescriptorImageInfo output_image_info;
	memset (&output_image_info, 0, sizeof (output_image_info));
	output_image_info.imageView = color_buffers_view[0];
	output_image_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

	VkDescriptorBufferInfo palette_octree_info;
	memset (&palette_octree_info, 0, sizeof (palette_octree_info));
	palette_octree_info.buffer = palette_octree_buffer;
	palette_octree_info.offset = 0;
	palette_octree_info.range = VK_WHOLE_SIZE;

	VkDescriptorImageInfo blue_noise_image_info;
	memset (&blue_noise_image_info, 0, sizeof (blue_noise_image_info));
	blue_noise_image_info.imageView = bluenoisetexture->image_view;
	blue_noise_image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	blue_noise_image_info.sampler = vulkan_globals.linear_sampler;

	VkWriteDescriptorSet screen_effects_writes[5];
	memset (screen_effects_writes, 0, sizeof (screen_effects_writes));
	screen_effects_writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	screen_effects_writes[0].dstBinding = 0;
	screen_effects_writes[0].dstArrayElement = 0;
	screen_effects_writes[0].descriptorCount = 1;
	screen_effects_writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	screen_effects_writes[0].dstSet = vulkan_globals.screen_effects_desc_set;
	screen_effects_writes[0].pImageInfo = &input_image_info;

	screen_effects_writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	screen_effects_writes[1].dstBinding = 1;
	screen_effects_writes[1].dstArrayElement = 0;
	screen_effects_writes[1].descriptorCount = 1;
	screen_effects_writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	screen_effects_writes[1].dstSet = vulkan_globals.screen_effects_desc_set;
	screen_effects_writes[1].pImageInfo = &blue_noise_image_info;

	screen_effects_writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	screen_effects_writes[2].dstBinding = 2;
	screen_effects_writes[2].dstArrayElement = 0;
	screen_effects_writes[2].descriptorCount = 1;
	screen_effects_writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	screen_effects_writes[2].dstSet = vulkan_globals.screen_effects_desc_set;
	screen_effects_writes[2].pImageInfo = &output_image_info;

	screen_effects_writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	screen_effects_writes[3].dstBinding = 3;
	screen_effects_writes[3].dstArrayElement = 0;
	screen_effects_writes[3].descriptorCount = 1;
	screen_effects_writes[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
	screen_effects_writes[3].dstSet = vulkan_globals.screen_effects_desc_set;
	screen_effects_writes[3].pTexelBufferView = &palette_buffer_view;

	screen_effects_writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	screen_effects_writes[4].dstBinding = 4;
	screen_effects_writes[4].dstArrayElement = 0;
	screen_effects_writes[4].descriptorCount = 1;
	screen_effects_writes[4].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	screen_effects_writes[4].dstSet = vulkan_globals.screen_effects_desc_set;
	screen_effects_writes[4].pBufferInfo = &palette_octree_info;

	vkUpdateDescriptorSets (vulkan_globals.device, 5, screen_effects_writes, 0, NULL);
}

/*
===============
GL_CreateSwapChain
===============
*/
static qboolean GL_CreateSwapChain (void)
{
	uint32_t i;
	VkResult err;

#if defined(VK_EXT_full_screen_exclusive)
	qboolean use_exclusive_full_screen = false;
	qboolean try_use_exclusive_full_screen =
		vulkan_globals.full_screen_exclusive && vulkan_globals.want_full_screen_exclusive && has_focus && VID_GetFullscreen ();
	VkSurfaceFullScreenExclusiveInfoEXT		 full_screen_exclusive_info;
	VkSurfaceFullScreenExclusiveWin32InfoEXT full_screen_exclusive_win32_info;
	if (try_use_exclusive_full_screen)
	{
		SDL_SysWMinfo wmInfo;
		HWND		  hwnd;
		HMONITOR	  monitor;

		SDL_VERSION (&wmInfo.version);
		SDL_GetWindowWMInfo (draw_context, &wmInfo);
		hwnd = wmInfo.info.win.window;

		monitor = MonitorFromWindow (hwnd, MONITOR_DEFAULTTOPRIMARY);

		memset (&full_screen_exclusive_win32_info, 0, sizeof (full_screen_exclusive_win32_info));
		full_screen_exclusive_win32_info.sType = VK_STRUCTURE_TYPE_SURFACE_FULL_SCREEN_EXCLUSIVE_WIN32_INFO_EXT;
		full_screen_exclusive_win32_info.pNext = NULL;
		full_screen_exclusive_win32_info.hmonitor = monitor;

		memset (&full_screen_exclusive_info, 0, sizeof (full_screen_exclusive_info));
		full_screen_exclusive_info.sType = VK_STRUCTURE_TYPE_SURFACE_FULL_SCREEN_EXCLUSIVE_INFO_EXT;
		full_screen_exclusive_info.pNext = &full_screen_exclusive_win32_info;
		full_screen_exclusive_info.fullScreenExclusive = VK_FULL_SCREEN_EXCLUSIVE_APPLICATION_CONTROLLED_EXT;

		VkPhysicalDeviceSurfaceInfo2KHR surface_info_2;
		memset (&surface_info_2, 0, sizeof (surface_info_2));
		surface_info_2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SURFACE_INFO_2_KHR;
		surface_info_2.pNext = &full_screen_exclusive_info;
		surface_info_2.surface = vulkan_surface;

		VkSurfaceCapabilitiesFullScreenExclusiveEXT surface_capabilities_full_screen_exclusive;
		memset (&surface_capabilities_full_screen_exclusive, 0, sizeof (surface_capabilities_full_screen_exclusive));
		surface_capabilities_full_screen_exclusive.sType = VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_FULL_SCREEN_EXCLUSIVE_EXT;
		surface_capabilities_full_screen_exclusive.fullScreenExclusiveSupported = VK_FALSE;

		VkSurfaceCapabilities2KHR surface_capabilitities_2;
		surface_capabilitities_2.sType = VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_2_KHR;
		surface_capabilitities_2.pNext = &surface_capabilities_full_screen_exclusive;

		err = fpGetPhysicalDeviceSurfaceCapabilities2KHR (vulkan_physical_device, &surface_info_2, &surface_capabilitities_2);
		if (err != VK_SUCCESS)
			Sys_Error ("Couldn't get surface capabilities");

		vulkan_surface_capabilities = surface_capabilitities_2.surfaceCapabilities;
		use_exclusive_full_screen = surface_capabilities_full_screen_exclusive.fullScreenExclusiveSupported;
	}
	else
#endif
	{
		err = fpGetPhysicalDeviceSurfaceCapabilitiesKHR (vulkan_physical_device, vulkan_surface, &vulkan_surface_capabilities);
		if (err != VK_SUCCESS)
			Sys_Error ("Couldn't get surface capabilities");
	}

	if ((vulkan_surface_capabilities.currentExtent.width != 0xFFFFFFFF || vulkan_surface_capabilities.currentExtent.height != 0xFFFFFFFF) &&
		(vulkan_surface_capabilities.currentExtent.width != vid.width || vulkan_surface_capabilities.currentExtent.height != vid.height))
	{
		return false;
	}

	uint32_t format_count;
	err = fpGetPhysicalDeviceSurfaceFormatsKHR (vulkan_physical_device, vulkan_surface, &format_count, NULL);
	if (err != VK_SUCCESS)
		Sys_Error ("Couldn't get surface formats");

	VkSurfaceFormatKHR *surface_formats = (VkSurfaceFormatKHR *)Mem_Alloc (format_count * sizeof (VkSurfaceFormatKHR));
	err = fpGetPhysicalDeviceSurfaceFormatsKHR (vulkan_physical_device, vulkan_surface, &format_count, surface_formats);
	if (err != VK_SUCCESS)
		Sys_Error ("fpGetPhysicalDeviceSurfaceFormatsKHR failed");

	VkFormat		swap_chain_format = VK_FORMAT_B8G8R8A8_UNORM;
	VkColorSpaceKHR swap_chain_color_space = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;

	if (surface_formats[0].format != VK_FORMAT_UNDEFINED || format_count > 1)
	{
		qboolean found_wanted_format = false;
		for (i = 0; i < format_count; ++i)
		{
			if (surface_formats[i].format == swap_chain_format && surface_formats[i].colorSpace == swap_chain_color_space)
			{
				found_wanted_format = true;
				break;
			}
		}

		// If we can't find VK_FORMAT_B8G8R8A8_UNORM/VK_COLOR_SPACE_SRGB_NONLINEAR_KHR select first entry
		// I doubt this will ever happen, but the spec doesn't guarantee it
		if (!found_wanted_format)
		{
			swap_chain_format = surface_formats[0].format;
			swap_chain_color_space = surface_formats[0].colorSpace;
		}
	}

	uint32_t present_mode_count = 0;
	err = fpGetPhysicalDeviceSurfacePresentModesKHR (vulkan_physical_device, vulkan_surface, &present_mode_count, NULL);
	if (err != VK_SUCCESS)
		Sys_Error ("fpGetPhysicalDeviceSurfacePresentModesKHR failed");

	VkPresentModeKHR *present_modes = (VkPresentModeKHR *)Mem_Alloc (present_mode_count * sizeof (VkPresentModeKHR));
	err = fpGetPhysicalDeviceSurfacePresentModesKHR (vulkan_physical_device, vulkan_surface, &present_mode_count, present_modes);
	if (err != VK_SUCCESS)
		Sys_Error ("fpGetPhysicalDeviceSurfacePresentModesKHR failed");

	// VK_PRESENT_MODE_FIFO_KHR is always supported
	VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;
	if (vid_vsync.value == 0)
	{
		qboolean found_immediate = false;
		qboolean found_mailbox = false;
		for (i = 0; i < present_mode_count; ++i)
		{
			if (present_modes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR)
				found_immediate = true;
			if (present_modes[i] == VK_PRESENT_MODE_MAILBOX_KHR)
				found_mailbox = true;
		}

		if (found_mailbox)
			present_mode = VK_PRESENT_MODE_MAILBOX_KHR;
		if (found_immediate)
			present_mode = VK_PRESENT_MODE_IMMEDIATE_KHR;
	}

	Mem_Free (present_modes);

	switch (present_mode)
	{
	case VK_PRESENT_MODE_FIFO_KHR:
		Sys_Printf ("Using FIFO present mode\n");
		break;
	case VK_PRESENT_MODE_MAILBOX_KHR:
		Sys_Printf ("Using MAILBOX present mode\n");
		break;
	case VK_PRESENT_MODE_IMMEDIATE_KHR:
		Sys_Printf ("Using IMMEDIATE present mode\n");
		break;
	default:
		break;
	}

	VkSwapchainCreateInfoKHR swapchain_create_info;
	memset (&swapchain_create_info, 0, sizeof (swapchain_create_info));
	swapchain_create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	swapchain_create_info.pNext = NULL;
	swapchain_create_info.surface = vulkan_surface;
	swapchain_create_info.minImageCount = (vid_vsync.value >= 2) ? 3 : 2;
	swapchain_create_info.imageFormat = swap_chain_format;
	swapchain_create_info.imageColorSpace = swap_chain_color_space;
	swapchain_create_info.imageExtent.width = vid.width;
	swapchain_create_info.imageExtent.height = vid.height;
	swapchain_create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	swapchain_create_info.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
	swapchain_create_info.imageArrayLayers = 1;
	swapchain_create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	swapchain_create_info.queueFamilyIndexCount = 0;
	swapchain_create_info.pQueueFamilyIndices = NULL;
	swapchain_create_info.presentMode = present_mode;
	swapchain_create_info.clipped = true;
	swapchain_create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	// Not all devices support ALPHA_OPAQUE
	if (!(vulkan_surface_capabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR))
		swapchain_create_info.compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;

	vulkan_globals.swap_chain_full_screen_exclusive = false;
	vulkan_globals.swap_chain_full_screen_acquired = false;

#if defined(VK_EXT_full_screen_exclusive)
	if (use_exclusive_full_screen)
	{
		swapchain_create_info.pNext = &full_screen_exclusive_info;
		vulkan_globals.swap_chain_full_screen_exclusive = true;
	}
#endif

	vulkan_globals.swap_chain_format = swap_chain_format;
	Mem_Free (surface_formats);

	assert (vulkan_swapchain == VK_NULL_HANDLE);
	err = fpCreateSwapchainKHR (vulkan_globals.device, &swapchain_create_info, NULL, &vulkan_swapchain);
	if (err != VK_SUCCESS)
	{
#if defined(VK_EXT_full_screen_exclusive)
		if (use_exclusive_full_screen)
		{
			// At least one person reported that a driver fails to create the swap chain even though it advertises full screen exclusivity
			swapchain_create_info.pNext = NULL;
			vulkan_globals.swap_chain_full_screen_exclusive = false;
			use_exclusive_full_screen = false;
			err = fpCreateSwapchainKHR (vulkan_globals.device, &swapchain_create_info, NULL, &vulkan_swapchain);
		}
#endif
		if (err != VK_SUCCESS)
		{
			Sys_Error ("Couldn't create swap chain");
		}
	}
	num_images_acquired = 0;

	for (i = 0; i < num_swap_chain_images; ++i)
		assert (swapchain_images[i] == VK_NULL_HANDLE);
	err = fpGetSwapchainImagesKHR (vulkan_globals.device, vulkan_swapchain, &num_swap_chain_images, NULL);
	if (err != VK_SUCCESS || num_swap_chain_images > MAX_SWAP_CHAIN_IMAGES)
		Sys_Error ("Couldn't get swap chain images");

	fpGetSwapchainImagesKHR (vulkan_globals.device, vulkan_swapchain, &num_swap_chain_images, swapchain_images);

	VkImageViewCreateInfo image_view_create_info;
	memset (&image_view_create_info, 0, sizeof (image_view_create_info));
	image_view_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	image_view_create_info.format = vulkan_globals.swap_chain_format;
	image_view_create_info.components.r = VK_COMPONENT_SWIZZLE_R;
	image_view_create_info.components.g = VK_COMPONENT_SWIZZLE_G;
	image_view_create_info.components.b = VK_COMPONENT_SWIZZLE_B;
	image_view_create_info.components.a = VK_COMPONENT_SWIZZLE_A;
	image_view_create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	image_view_create_info.subresourceRange.baseMipLevel = 0;
	image_view_create_info.subresourceRange.levelCount = 1;
	image_view_create_info.subresourceRange.baseArrayLayer = 0;
	image_view_create_info.subresourceRange.layerCount = 1;
	image_view_create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
	image_view_create_info.flags = 0;

	VkSemaphoreCreateInfo semaphore_create_info;
	memset (&semaphore_create_info, 0, sizeof (semaphore_create_info));
	semaphore_create_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

	for (i = 0; i < num_swap_chain_images; ++i)
	{
		GL_SetObjectName ((uint64_t)swapchain_images[i], VK_OBJECT_TYPE_IMAGE, "Swap Chain");

		assert (swapchain_images_views[i] == VK_NULL_HANDLE);
		image_view_create_info.image = swapchain_images[i];
		err = vkCreateImageView (vulkan_globals.device, &image_view_create_info, NULL, &swapchain_images_views[i]);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateImageView failed");

		GL_SetObjectName ((uint64_t)swapchain_images_views[i], VK_OBJECT_TYPE_IMAGE_VIEW, "Swap Chain View");
	}

	for (i = 0; i < DOUBLE_BUFFERED; ++i)
	{
		assert (image_aquired_semaphores[i] == VK_NULL_HANDLE);
		err = vkCreateSemaphore (vulkan_globals.device, &semaphore_create_info, NULL, &image_aquired_semaphores[i]);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateSemaphore failed");
	}

	return true;
}

/*
===============
GL_CreateFrameBuffers
===============
*/
static void GL_CreateFrameBuffers (void)
{
	uint32_t i;

	Sys_Printf ("Creating frame buffers\n");

	VkResult err;

	const qboolean resolve = (vulkan_globals.sample_count != VK_SAMPLE_COUNT_1_BIT);

	for (i = 0; i < NUM_COLOR_BUFFERS; ++i)
	{
		VkFramebufferCreateInfo framebuffer_create_info;
		memset (&framebuffer_create_info, 0, sizeof (framebuffer_create_info));
		framebuffer_create_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		framebuffer_create_info.renderPass = vulkan_globals.secondary_cb_contexts[CBX_WORLD_0].render_pass;
		framebuffer_create_info.attachmentCount = resolve ? 3 : 2;
		framebuffer_create_info.width = vid.width;
		framebuffer_create_info.height = vid.height;
		framebuffer_create_info.layers = 1;

		VkImageView attachments[3] = {color_buffers_view[i], depth_buffer_view, msaa_color_buffer_view};
		framebuffer_create_info.pAttachments = attachments;

		assert (main_framebuffers[i] == VK_NULL_HANDLE);
		err = vkCreateFramebuffer (vulkan_globals.device, &framebuffer_create_info, NULL, &main_framebuffers[i]);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateFramebuffer failed");

		GL_SetObjectName ((uint64_t)main_framebuffers[i], VK_OBJECT_TYPE_FRAMEBUFFER, "main");
	}

	for (i = 0; i < num_swap_chain_images; ++i)
	{
		VkFramebufferCreateInfo framebuffer_create_info;
		memset (&framebuffer_create_info, 0, sizeof (framebuffer_create_info));
		framebuffer_create_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		framebuffer_create_info.renderPass = vulkan_globals.secondary_cb_contexts[CBX_GUI].render_pass;
		framebuffer_create_info.attachmentCount = 2;
		framebuffer_create_info.width = vid.width;
		framebuffer_create_info.height = vid.height;
		framebuffer_create_info.layers = 1;

		VkImageView attachments[2] = {color_buffers_view[0], swapchain_images_views[i]};
		framebuffer_create_info.pAttachments = attachments;

		assert (ui_framebuffers[i] == VK_NULL_HANDLE);
		err = vkCreateFramebuffer (vulkan_globals.device, &framebuffer_create_info, NULL, &ui_framebuffers[i]);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateFramebuffer failed");

		GL_SetObjectName ((uint64_t)ui_framebuffers[i], VK_OBJECT_TYPE_FRAMEBUFFER, "ui");
	}
}

/*
===============
GL_CreateRenderResources
===============
*/
static void GL_CreateRenderResources (void)
{
	if (!GL_CreateSwapChain ())
	{
		render_resources_created = false;
		return;
	}

	GL_CreateColorBuffer ();
	GL_CreateDepthBuffer ();
	GL_CreateRenderPasses ();
	GL_CreateFrameBuffers ();
	R_CreatePipelines ();
	GL_UpdateDescriptorSets ();

	render_resources_created = true;
}

/*
===============
GL_DestroyRenderResources
===============
*/
static void GL_DestroyRenderResources (void)
{
	uint32_t i;

	render_resources_created = false;

	GL_WaitForDeviceIdle ();

	R_DestroyPipelines ();

	R_FreeDescriptorSet (postprocess_descriptor_set, &vulkan_globals.input_attachment_set_layout);
	postprocess_descriptor_set = VK_NULL_HANDLE;

	R_FreeDescriptorSet (vulkan_globals.screen_effects_desc_set, &vulkan_globals.screen_effects_set_layout);
	vulkan_globals.screen_effects_desc_set = VK_NULL_HANDLE;

	if (msaa_color_buffer)
	{
		vkDestroyImageView (vulkan_globals.device, msaa_color_buffer_view, NULL);
		vkDestroyImage (vulkan_globals.device, msaa_color_buffer, NULL);
		Atomic_DecrementUInt32 (&num_vulkan_misc_allocations);
		R_FreeVulkanMemory (&msaa_color_buffer_memory);

		msaa_color_buffer_view = VK_NULL_HANDLE;
		msaa_color_buffer = VK_NULL_HANDLE;
	}

	for (i = 0; i < NUM_COLOR_BUFFERS; ++i)
	{
		vkDestroyImageView (vulkan_globals.device, color_buffers_view[i], NULL);
		vkDestroyImage (vulkan_globals.device, vulkan_globals.color_buffers[i], NULL);
		Atomic_DecrementUInt32 (&num_vulkan_misc_allocations);
		R_FreeVulkanMemory (&color_buffers_memory[i]);

		color_buffers_view[i] = VK_NULL_HANDLE;
		vulkan_globals.color_buffers[i] = VK_NULL_HANDLE;
	}

	vkDestroyImageView (vulkan_globals.device, depth_buffer_view, NULL);
	vkDestroyImage (vulkan_globals.device, depth_buffer, NULL);
	Atomic_DecrementUInt32 (&num_vulkan_misc_allocations);
	R_FreeVulkanMemory (&depth_buffer_memory);

	depth_buffer_view = VK_NULL_HANDLE;
	depth_buffer = VK_NULL_HANDLE;

	for (i = 0; i < NUM_COLOR_BUFFERS; ++i)
	{
		vkDestroyFramebuffer (vulkan_globals.device, main_framebuffers[i], NULL);
		main_framebuffers[i] = VK_NULL_HANDLE;
	}

	for (i = 0; i < num_swap_chain_images; ++i)
	{
		vkDestroyImageView (vulkan_globals.device, swapchain_images_views[i], NULL);
		swapchain_images_views[i] = VK_NULL_HANDLE;
		vkDestroyFramebuffer (vulkan_globals.device, ui_framebuffers[i], NULL);
		ui_framebuffers[i] = VK_NULL_HANDLE;

		// Swapchain images do not need to be destroyed
		swapchain_images[i] = VK_NULL_HANDLE;
	}

	for (i = 0; i < DOUBLE_BUFFERED; ++i)
	{
		vkDestroySemaphore (vulkan_globals.device, image_aquired_semaphores[i], NULL);
		image_aquired_semaphores[i] = VK_NULL_HANDLE;
	}

	fpDestroySwapchainKHR (vulkan_globals.device, vulkan_swapchain, NULL);
	vulkan_swapchain = VK_NULL_HANDLE;

	vkDestroyRenderPass (vulkan_globals.device, vulkan_globals.secondary_cb_contexts[CBX_GUI].render_pass, NULL);
	for (int cbx_index = CBX_GUI; cbx_index <= CBX_POST_PROCESS; ++cbx_index)
		vulkan_globals.secondary_cb_contexts[cbx_index].render_pass = VK_NULL_HANDLE;
	vkDestroyRenderPass (vulkan_globals.device, vulkan_globals.main_render_pass[0], NULL);
	vkDestroyRenderPass (vulkan_globals.device, vulkan_globals.main_render_pass[1], NULL);
	vulkan_globals.main_render_pass[0] = VK_NULL_HANDLE;
	vulkan_globals.main_render_pass[1] = VK_NULL_HANDLE;
	for (int cbx_index = CBX_WORLD_0; cbx_index <= CBX_VIEW_MODEL; ++cbx_index)
		vulkan_globals.secondary_cb_contexts[cbx_index].render_pass = VK_NULL_HANDLE;
}

/*
=================
GL_BeginRenderingTask
=================
*/
void GL_BeginRenderingTask (void *unused)
{
	VkResult err;

	if (frame_submitted[current_cb_index])
	{
		err = vkWaitForFences (vulkan_globals.device, 1, &command_buffer_fences[current_cb_index], VK_TRUE, UINT64_MAX);
		if (err != VK_SUCCESS)
			Sys_Error ("vkWaitForFences failed");
	}

	err = vkResetFences (vulkan_globals.device, 1, &command_buffer_fences[current_cb_index]);
	if (err != VK_SUCCESS)
		Sys_Error ("vkResetFences failed");

	R_CollectDynamicBufferGarbage ();
	R_CollectMeshBufferGarbage ();
	TexMgr_CollectGarbage ();

	{
		cb_context_t *cbx = &vulkan_globals.primary_cb_context;
		cbx->cb = primary_command_buffers[current_cb_index];
		cbx->current_canvas = CANVAS_INVALID;
		memset (&cbx->current_pipeline, 0, sizeof (cbx->current_pipeline));

		VkCommandBufferBeginInfo command_buffer_begin_info;
		memset (&command_buffer_begin_info, 0, sizeof (command_buffer_begin_info));
		command_buffer_begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		command_buffer_begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

		err = vkBeginCommandBuffer (cbx->cb, &command_buffer_begin_info);
		if (err != VK_SUCCESS)
			Sys_Error ("vkBeginCommandBuffer failed");
	}

	for (int cbx_index = 0; cbx_index < CBX_NUM; ++cbx_index)
	{
		cb_context_t *cbx = &vulkan_globals.secondary_cb_contexts[cbx_index];
		cbx->cb = secondary_command_buffers[cbx_index][current_cb_index];
		cbx->current_canvas = CANVAS_INVALID;
		memset (&cbx->current_pipeline, 0, sizeof (cbx->current_pipeline));

		if (cbx_index <= CBX_VIEW_MODEL)
		{
			const int main_render_pass_index = Sky_NeedStencil () ? 0 : 1;
			cbx->render_pass = vulkan_globals.main_render_pass[main_render_pass_index];
		}

		VkCommandBufferInheritanceInfo inheritance_info;
		memset (&inheritance_info, 0, sizeof (inheritance_info));
		inheritance_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
		inheritance_info.renderPass = cbx->render_pass;
		inheritance_info.subpass = cbx->subpass;

		VkCommandBufferBeginInfo command_buffer_begin_info;
		memset (&command_buffer_begin_info, 0, sizeof (command_buffer_begin_info));
		command_buffer_begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		command_buffer_begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		if (cbx_index != CBX_UPDATE_LIGHTMAPS)
			command_buffer_begin_info.flags |= VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;
		command_buffer_begin_info.pInheritanceInfo = &inheritance_info;

		err = vkBeginCommandBuffer (cbx->cb, &command_buffer_begin_info);
		if (err != VK_SUCCESS)
			Sys_Error ("vkBeginCommandBuffer failed");

		if (cbx_index == CBX_UPDATE_LIGHTMAPS)
			continue;

		VkRect2D render_area;
		render_area.offset.x = 0;
		render_area.offset.y = 0;
		render_area.extent.width = vid.width;
		render_area.extent.height = vid.height;
		vkCmdSetScissor (cbx->cb, 0, 1, &render_area);

		VkViewport viewport;
		viewport.x = 0;
		viewport.y = 0;
		viewport.width = vid.width;
		viewport.height = vid.height;
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;
		vkCmdSetViewport (cbx->cb, 0, 1, &viewport);

		R_BindPipeline (cbx, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.basic_blend_pipeline[cbx->render_pass_index]);
		GL_SetCanvas (cbx, CANVAS_NONE);
	}

	R_SwapDynamicBuffers ();
}

/*
=================
GL_SynchronizeEndRenderingTask
=================
*/
void GL_SynchronizeEndRenderingTask (void)
{
	if (prev_end_rendering_task != INVALID_TASK_HANDLE)
	{
		Task_Join (prev_end_rendering_task, SDL_MUTEX_MAXWAIT);
		prev_end_rendering_task = INVALID_TASK_HANDLE;
	}
}

/*
=================
GL_BeginRendering
=================
*/
qboolean GL_BeginRendering (qboolean use_tasks, task_handle_t *begin_rendering_task, int *width, int *height)
{
	if (!use_tasks)
		GL_SynchronizeEndRenderingTask ();

	if (vid.restart_next_frame)
	{
		VID_Restart (false);
		vid.restart_next_frame = false;
	}

	if (!render_resources_created)
	{
		GL_CreateRenderResources ();

		if (!render_resources_created)
		{
			return false;
		}
	}

	*width = vid.width;
	*height = vid.height;

	if (use_tasks)
		*begin_rendering_task = Task_AllocateAndAssignFunc (GL_BeginRenderingTask, NULL, 0);
	else
		GL_BeginRenderingTask (NULL);

	return true;
}

/*
=================
GL_AcquireNextSwapChainImage
=================
*/
qboolean GL_AcquireNextSwapChainImage (void)
{
	if (num_images_acquired >= (num_swap_chain_images - 1))
	{
		return false;
	}

#if defined(VK_EXT_full_screen_exclusive)
	if (VID_GetFullscreen () && vulkan_globals.want_full_screen_exclusive && vulkan_globals.swap_chain_full_screen_exclusive &&
		!vulkan_globals.swap_chain_full_screen_acquired)
	{
		const VkResult result = fpAcquireFullScreenExclusiveModeEXT (vulkan_globals.device, vulkan_swapchain);
		if (result == VK_SUCCESS)
		{
			vulkan_globals.swap_chain_full_screen_acquired = true;
			Sys_Printf ("Full screen exclusive acquired\n");
		}
	}
	else if (!vulkan_globals.want_full_screen_exclusive && vulkan_globals.swap_chain_full_screen_exclusive && vulkan_globals.swap_chain_full_screen_acquired)
	{
		const VkResult result = fpReleaseFullScreenExclusiveModeEXT (vulkan_globals.device, vulkan_swapchain);
		if (result == VK_SUCCESS)
		{
			vulkan_globals.swap_chain_full_screen_acquired = false;
			Sys_Printf ("Full screen exclusive released\n");
		}
	}
#endif

	VkResult err = fpAcquireNextImageKHR (
		vulkan_globals.device, vulkan_swapchain, UINT64_MAX, image_aquired_semaphores[current_cb_index], VK_NULL_HANDLE, &current_swapchain_buffer);
#if defined(VK_EXT_full_screen_exclusive)
	if ((err == VK_ERROR_OUT_OF_DATE_KHR) || (err == VK_ERROR_SURFACE_LOST_KHR) || (err == VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT))
#else
	if ((err == VK_ERROR_OUT_OF_DATE_KHR) || (err == VK_ERROR_SURFACE_LOST_KHR))
#endif
	{
		vid.restart_next_frame = true;
		return false;
	}
	else if (err == VK_SUBOPTIMAL_KHR)
	{
		vid.restart_next_frame = true;
	}
	else if (err != VK_SUCCESS)
		Sys_Error ("Couldn't acquire next image");

	num_images_acquired += 1;
	return true;
}

typedef struct screen_effect_constants_s
{
	uint32_t clamp_size_x;
	uint32_t clamp_size_y;
	float	 screen_size_rcp_x;
	float	 screen_size_rcp_y;
	float	 aspect_ratio;
	float	 time;
	uint32_t flags;
	float	 poly_blend_r;
	float	 poly_blend_g;
	float	 poly_blend_b;
	float	 poly_blend_a;
} screen_effect_constants_t;

typedef struct end_rendering_parms_s
{
	uint32_t vid_width	   : 20;
	qboolean swapchain	   : 1;
	qboolean render_warp   : 1;
	qboolean vid_palettize : 1;
	uint32_t render_scale  : 4;
	uint32_t vid_height	   : 20;
	float	 time;
	uint8_t	 v_blend[4];
} end_rendering_parms_t;

#define SCREEN_EFFECT_FLAG_SCALE_MASK 0x3
#define SCREEN_EFFECT_FLAG_SCALE_2X	  0x1
#define SCREEN_EFFECT_FLAG_SCALE_4X	  0x2
#define SCREEN_EFFECT_FLAG_SCALE_8X	  0x3
#define SCREEN_EFFECT_FLAG_WATER_WARP 0x4
#define SCREEN_EFFECT_FLAG_PALETTIZE  0x8

/*
===============
GL_ScreenEffects
===============
*/
static void GL_ScreenEffects (cb_context_t *cbx, qboolean enabled, end_rendering_parms_t *parms)
{
	if (enabled)
	{
		R_BeginDebugUtilsLabel (cbx, "Screen Effects");

		VkImageMemoryBarrier image_barriers[2];
		image_barriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		image_barriers[0].pNext = NULL;
		image_barriers[0].srcAccessMask = 0;
		image_barriers[0].dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		image_barriers[0].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		image_barriers[0].newLayout = VK_IMAGE_LAYOUT_GENERAL;
		image_barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		image_barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		image_barriers[0].image = vulkan_globals.color_buffers[0];
		image_barriers[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		image_barriers[0].subresourceRange.baseMipLevel = 0;
		image_barriers[0].subresourceRange.levelCount = 1;
		image_barriers[0].subresourceRange.baseArrayLayer = 0;
		image_barriers[0].subresourceRange.layerCount = 1;

		image_barriers[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		image_barriers[1].pNext = NULL;
		image_barriers[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		image_barriers[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		image_barriers[1].oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		image_barriers[1].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		image_barriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		image_barriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		image_barriers[1].image = vulkan_globals.color_buffers[1];
		image_barriers[1].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		image_barriers[1].subresourceRange.baseMipLevel = 0;
		image_barriers[1].subresourceRange.levelCount = 1;
		image_barriers[1].subresourceRange.baseArrayLayer = 0;
		image_barriers[1].subresourceRange.layerCount = 1;

		vkCmdPipelineBarrier (
			cbx->cb, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, NULL, 0, NULL, 2, image_barriers);

		GL_SetCanvas (cbx, CANVAS_NONE); // Invalidate canvas so push constants get set later

		vulkan_pipeline_t *pipeline = NULL;
		if (parms->render_scale >= 2)
		{
			if (vulkan_globals.screen_effects_sops && r_usesops.value)
				pipeline = &vulkan_globals.screen_effects_scale_sops_pipeline;
			else
				pipeline = &vulkan_globals.screen_effects_scale_pipeline;
		}
		else
			pipeline = &vulkan_globals.screen_effects_pipeline;

		R_BindPipeline (cbx, VK_PIPELINE_BIND_POINT_COMPUTE, *pipeline);
		vkCmdBindDescriptorSets (cbx->cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->layout.handle, 0, 1, &vulkan_globals.screen_effects_desc_set, 0, NULL);

		uint32_t screen_effect_flags = 0;
		if (parms->render_warp)
			screen_effect_flags |= SCREEN_EFFECT_FLAG_WATER_WARP;
		if (parms->render_scale >= 8)
			screen_effect_flags |= SCREEN_EFFECT_FLAG_SCALE_8X;
		else if (parms->render_scale >= 4)
			screen_effect_flags |= SCREEN_EFFECT_FLAG_SCALE_4X;
		else if (parms->render_scale >= 2)
			screen_effect_flags |= SCREEN_EFFECT_FLAG_SCALE_2X;
		if (parms->vid_palettize)
			screen_effect_flags |= SCREEN_EFFECT_FLAG_PALETTIZE;
		const screen_effect_constants_t push_constants = {
			parms->vid_width - 1,
			parms->vid_height - 1,
			1.0f / (float)parms->vid_width,
			1.0f / (float)parms->vid_height,
			(float)parms->vid_width / (float)parms->vid_height,
			parms->time,
			screen_effect_flags,
			(float)parms->v_blend[0] / 255.0f,
			(float)parms->v_blend[1] / 255.0f,
			(float)parms->v_blend[2] / 255.0f,
			(float)parms->v_blend[3] / 255.0f};
		R_PushConstants (cbx, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof (screen_effect_constants_t), &push_constants);

		vkCmdDispatch (cbx->cb, (parms->vid_width + 7) / 8, (parms->vid_height + 7) / 8, 1);

		image_barriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		image_barriers[0].pNext = NULL;
		image_barriers[0].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		image_barriers[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		image_barriers[0].oldLayout = VK_IMAGE_LAYOUT_GENERAL;
		image_barriers[0].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		image_barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		image_barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		image_barriers[0].image = vulkan_globals.color_buffers[0];
		image_barriers[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		image_barriers[0].subresourceRange.baseMipLevel = 0;
		image_barriers[0].subresourceRange.levelCount = 1;
		image_barriers[0].subresourceRange.baseArrayLayer = 0;
		image_barriers[0].subresourceRange.layerCount = 1;

		vkCmdPipelineBarrier (
			cbx->cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, NULL, 0, NULL, 1, image_barriers);

		R_EndDebugUtilsLabel (cbx);
	}
	else
	{
		VkMemoryBarrier memory_barrier;
		memory_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
		memory_barrier.pNext = NULL;
		memory_barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		memory_barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

		vkCmdPipelineBarrier (
			cbx->cb, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 1, &memory_barrier, 0, NULL, 0, NULL);
	}
}

/*
=================
GL_EndRenderingTask
=================
*/
static void GL_EndRenderingTask (end_rendering_parms_t *parms)
{
	R_SubmitStagingBuffers ();
	R_FlushDynamicBuffers ();

	VkResult err;
	int		 cb_index = current_cb_index;

	qboolean swapchain_acquired = parms->swapchain && GL_AcquireNextSwapChainImage ();
	if (swapchain_acquired == true)
	{
		cb_context_t *cbx = &vulkan_globals.secondary_cb_contexts[CBX_POST_PROCESS];

		// Render post process
		GL_Viewport (cbx, 0, 0, vid.width, vid.height, 0.0f, 1.0f);
		float postprocess_values[2] = {vid_gamma.value, q_min (2.0f, q_max (1.0f, vid_contrast.value))};

		R_BindPipeline (cbx, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.postprocess_pipeline);
		vkCmdBindDescriptorSets (
			cbx->cb, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.postprocess_pipeline.layout.handle, 0, 1, &postprocess_descriptor_set, 0, NULL);
		R_PushConstants (cbx, VK_SHADER_STAGE_FRAGMENT_BIT, 0, 2 * sizeof (float), postprocess_values);
		vkCmdDraw (cbx->cb, 3, 1, 0, 0);
	}

	for (int cbx_index = 0; cbx_index < CBX_NUM; ++cbx_index)
	{
		cb_context_t *cbx = &vulkan_globals.secondary_cb_contexts[cbx_index];
		err = vkEndCommandBuffer (cbx->cb);
		if (err != VK_SUCCESS)
			Sys_Error ("vkEndCommandBuffer failed");
	}

	VkCommandBuffer primary_cb = vulkan_globals.primary_cb_context.cb;

	VkRect2D render_area;
	render_area.offset.x = 0;
	render_area.offset.y = 0;
	render_area.extent.width = parms->vid_width;
	render_area.extent.height = parms->vid_height;

	VkClearValue depth_clear_value;
	depth_clear_value.depthStencil.depth = 0.0f;
	depth_clear_value.depthStencil.stencil = 0;

	VkClearValue clear_values[3];
	clear_values[0] = vulkan_globals.color_clear_value;
	clear_values[1] = depth_clear_value;
	clear_values[2] = vulkan_globals.color_clear_value;

	vkCmdExecuteCommands (primary_cb, 1, &vulkan_globals.secondary_cb_contexts[CBX_UPDATE_LIGHTMAPS].cb);

	const qboolean screen_effects = parms->render_warp || (parms->render_scale >= 2) || parms->vid_palettize || (gl_polyblend.value && parms->v_blend[3]);
	{
		const qboolean		  resolve = (vulkan_globals.sample_count != VK_SAMPLE_COUNT_1_BIT);
		VkRenderPassBeginInfo render_pass_begin_info;
		memset (&render_pass_begin_info, 0, sizeof (render_pass_begin_info));
		render_pass_begin_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		render_pass_begin_info.renderPass = vulkan_globals.secondary_cb_contexts[CBX_WORLD_0].render_pass;
		render_pass_begin_info.framebuffer = main_framebuffers[screen_effects ? 1 : 0];
		render_pass_begin_info.renderArea = render_area;
		render_pass_begin_info.clearValueCount = resolve ? 3 : 2;
		render_pass_begin_info.pClearValues = clear_values;
		vkCmdBeginRenderPass (primary_cb, &render_pass_begin_info, VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);
		int viewmodel_first = ENTALPHA_DECODE (cl.viewent.alpha) == 1.0f && !r_showtris.value && !r_showbboxes.value; // debug views are in the viewmodel's CB
		if (viewmodel_first)
			vkCmdExecuteCommands (primary_cb, 1, &vulkan_globals.secondary_cb_contexts[CBX_VIEW_MODEL].cb);
		for (int cbx_index = CBX_WORLD_0; cbx_index <= CBX_VIEW_MODEL - viewmodel_first; ++cbx_index)
			vkCmdExecuteCommands (primary_cb, 1, &vulkan_globals.secondary_cb_contexts[cbx_index].cb);
		vkCmdEndRenderPass (primary_cb);
	}

	GL_ScreenEffects (&vulkan_globals.primary_cb_context, screen_effects, parms);

	{
		VkRenderPassBeginInfo render_pass_begin_info;
		memset (&render_pass_begin_info, 0, sizeof (render_pass_begin_info));
		render_pass_begin_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		render_pass_begin_info.renderPass = vulkan_globals.secondary_cb_contexts[CBX_GUI].render_pass;
		render_pass_begin_info.framebuffer = ui_framebuffers[current_swapchain_buffer];
		render_pass_begin_info.renderArea = render_area;
		render_pass_begin_info.clearValueCount = 0;
		vkCmdBeginRenderPass (primary_cb, &render_pass_begin_info, VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);
		vkCmdExecuteCommands (primary_cb, 1, &vulkan_globals.secondary_cb_contexts[CBX_GUI].cb);
		vkCmdNextSubpass (primary_cb, VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);
		vkCmdExecuteCommands (primary_cb, 1, &vulkan_globals.secondary_cb_contexts[CBX_POST_PROCESS].cb);
		vkCmdEndRenderPass (primary_cb);
	}

	{
		VkSubmitInfo submit_info;
		err = vkEndCommandBuffer (primary_cb);
		if (err != VK_SUCCESS)
			Sys_Error ("vkEndCommandBuffer failed");

		memset (&submit_info, 0, sizeof (VkSubmitInfo));
		submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submit_info.commandBufferCount = 1;
		submit_info.pCommandBuffers = &primary_cb;
		submit_info.waitSemaphoreCount = swapchain_acquired ? 1 : 0;
		submit_info.pWaitSemaphores = &image_aquired_semaphores[cb_index];
		submit_info.signalSemaphoreCount = swapchain_acquired ? 1 : 0;
		submit_info.pSignalSemaphores = &draw_complete_semaphores[cb_index];
		VkPipelineStageFlags wait_dst_stage_mask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		submit_info.pWaitDstStageMask = &wait_dst_stage_mask;

		err = vkQueueSubmit (vulkan_globals.queue, 1, &submit_info, command_buffer_fences[cb_index]);
		if (err != VK_SUCCESS)
			Sys_Error ("vkQueueSubmit failed");
	}

	vulkan_globals.device_idle = false;

	if (swapchain_acquired == true)
	{
		VkPresentInfoKHR present_info;
		memset (&present_info, 0, sizeof (present_info));
		present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		present_info.swapchainCount = 1;
		present_info.pSwapchains = &vulkan_swapchain, present_info.pImageIndices = &current_swapchain_buffer;
		present_info.waitSemaphoreCount = 1;
		present_info.pWaitSemaphores = &draw_complete_semaphores[cb_index];
		err = fpQueuePresentKHR (vulkan_globals.queue, &present_info);
#if defined(VK_EXT_full_screen_exclusive)
		if ((err == VK_ERROR_OUT_OF_DATE_KHR) || (err == VK_ERROR_SURFACE_LOST_KHR) || (err == VK_SUBOPTIMAL_KHR) ||
			(err == VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT))
#else
		if ((err == VK_ERROR_OUT_OF_DATE_KHR) || (err == VK_ERROR_SURFACE_LOST_KHR) || (err == VK_SUBOPTIMAL_KHR))
#endif
		{
			vid.restart_next_frame = true;
		}
		else if (err != VK_SUCCESS)
			Sys_Error ("vkQueuePresentKHR failed");

		if (err == VK_SUCCESS || err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_ERROR_SURFACE_LOST_KHR)
			num_images_acquired -= 1;
	}

	frame_submitted[cb_index] = true;
	current_cb_index = (current_cb_index + 1) % DOUBLE_BUFFERED;
}

/*
=================
GL_EndRendering
=================
*/
task_handle_t GL_EndRendering (qboolean use_tasks, qboolean swapchain)
{
	end_rendering_parms_t parms = {
		.swapchain = swapchain,
		.render_warp = render_warp,
		.vid_palettize = vid_palettize.value != 0,
		.render_scale = CLAMP (0, render_scale, 8),
		.vid_width = vid.width,
		.vid_height = vid.height,
		.time = fmod (cl.time, 2.0 * M_PI),
		.v_blend[0] = v_blend[0],
		.v_blend[1] = v_blend[1],
		.v_blend[2] = v_blend[2],
		.v_blend[3] = v_blend[3],
	};
	task_handle_t end_rendering_task = INVALID_TASK_HANDLE;
	if (use_tasks)
		end_rendering_task = Task_AllocateAndAssignFunc ((task_func_t)GL_EndRenderingTask, &parms, sizeof (parms));
	else
		GL_EndRenderingTask (&parms);
	return end_rendering_task;
}

/*
=================
GL_WaitForDeviceIdle
=================
*/
void GL_WaitForDeviceIdle (void)
{
	assert (!Tasks_IsWorker ());
	GL_SynchronizeEndRenderingTask ();
	if (!vulkan_globals.device_idle)
	{
		R_SubmitStagingBuffers ();
		vkDeviceWaitIdle (vulkan_globals.device);
	}

	vulkan_globals.device_idle = true;
}

/*
=================
VID_Shutdown
=================
*/
void VID_Shutdown (void)
{
	if (vid_initialized)
	{
		SDL_QuitSubSystem (SDL_INIT_VIDEO);
		draw_context = NULL;
		PL_VID_Shutdown ();
	}
}

/*
===================================================================

MAIN WINDOW

===================================================================
*/

/*
================
ClearAllStates
================
*/
static void ClearAllStates (void)
{
	Key_ClearStates ();
	IN_ClearStates ();
}

//==========================================================================
//
//  COMMANDS
//
//==========================================================================

/*
=================
VID_DescribeCurrentMode_f
=================
*/
static void VID_DescribeCurrentMode_f (void)
{
	if (draw_context)
		Con_Printf (
			"%dx%dx%d %dHz %s\n", VID_GetCurrentWidth (), VID_GetCurrentHeight (), VID_GetCurrentBPP (), VID_GetCurrentRefreshRate (),
			VID_GetFullscreen () ? "fullscreen" : "windowed");
}

/*
=================
VID_DescribeModes_f -- johnfitz -- changed formatting, and added refresh rates after each mode.
=================
*/
static void VID_DescribeModes_f (void)
{
	int i;
	int lastwidth, lastheight, count;

	lastwidth = lastheight = count = 0;

	for (i = 0; i < nummodes; i++)
	{
		if (lastwidth != modelist[i].width || lastheight != modelist[i].height)
		{
			if (count > 0)
				Con_SafePrintf ("\n");
			Con_SafePrintf ("   %4i x %4i : %i", modelist[i].width, modelist[i].height, modelist[i].refreshrate);
			lastwidth = modelist[i].width;
			lastheight = modelist[i].height;
			count++;
		}
	}
	Con_Printf ("\n%i modes\n", count);
}

//==========================================================================
//
//  INIT
//
//==========================================================================

/*
=================
VID_InitModelist
=================
*/
static void VID_InitModelist (void)
{
	const int sdlmodes = SDL_GetNumDisplayModes (0);
	int		  i;

	modelist = Mem_Realloc (modelist, sizeof (vmode_t) * sdlmodes);
	nummodes = 0;
	for (i = 0; i < sdlmodes; i++)
	{
		SDL_DisplayMode mode;

		if (SDL_GetDisplayMode (0, i, &mode) == 0)
		{
			modelist[nummodes].width = mode.w;
			modelist[nummodes].height = mode.h;
			modelist[nummodes].refreshrate = mode.refresh_rate;
			nummodes++;
		}
	}
}

/*
=================
R_CreatePaletteOctreeBuffers
=================
*/
static void R_CreatePaletteOctreeBuffers (uint32_t *colors, int num_colors, palette_octree_node_t *nodes, int num_nodes)
{
	{
		const int colors_size = num_colors * sizeof (uint32_t);

		// Allocate palette buffer & upload to GPU
		VkBufferCreateInfo buffer_create_info;
		memset (&buffer_create_info, 0, sizeof (buffer_create_info));
		buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		buffer_create_info.size = colors_size;
		buffer_create_info.usage = VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		VkResult err = vkCreateBuffer (vulkan_globals.device, &buffer_create_info, NULL, &palette_colors_buffer);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateBuffer failed");
		GL_SetObjectName ((uint64_t)palette_colors_buffer, VK_OBJECT_TYPE_BUFFER, "Palette colors");

		VkMemoryRequirements memory_requirements;
		vkGetBufferMemoryRequirements (vulkan_globals.device, palette_colors_buffer, &memory_requirements);

		VkMemoryAllocateInfo memory_allocate_info;
		memset (&memory_allocate_info, 0, sizeof (memory_allocate_info));
		memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		memory_allocate_info.allocationSize = memory_requirements.size;
		memory_allocate_info.memoryTypeIndex = GL_MemoryTypeFromProperties (memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);

		Atomic_IncrementUInt32 (&num_vulkan_misc_allocations);
		vulkan_memory_t memory;
		R_AllocateVulkanMemory (&memory, &memory_allocate_info, VULKAN_MEMORY_TYPE_HOST);
		GL_SetObjectName ((uint64_t)memory.handle, VK_OBJECT_TYPE_DEVICE_MEMORY, "Palette colors");

		err = vkBindBufferMemory (vulkan_globals.device, palette_colors_buffer, memory.handle, 0);
		if (err != VK_SUCCESS)
			Sys_Error ("vkBindBufferMemory failed");

		VkBuffer		staging_buffer;
		VkCommandBuffer command_buffer;
		int				staging_offset;
		uint32_t	   *staging_memory = (uint32_t *)R_StagingAllocate (colors_size, 1, &command_buffer, &staging_buffer, &staging_offset);

		VkBufferCopy region;
		region.srcOffset = staging_offset;
		region.dstOffset = 0;
		region.size = colors_size;
		vkCmdCopyBuffer (command_buffer, staging_buffer, palette_colors_buffer, 1, &region);

		R_StagingBeginCopy ();
		memcpy (staging_memory, colors, colors_size);
		R_StagingEndCopy ();

		VkBufferViewCreateInfo buffer_view_create_info;
		memset (&buffer_view_create_info, 0, sizeof (buffer_view_create_info));
		buffer_view_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO;
		buffer_view_create_info.buffer = palette_colors_buffer;
		buffer_view_create_info.format = VK_FORMAT_R8G8B8A8_UNORM;
		buffer_view_create_info.range = VK_WHOLE_SIZE;
		err = vkCreateBufferView (vulkan_globals.device, &buffer_view_create_info, NULL, &palette_buffer_view);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateBufferView failed");
		GL_SetObjectName ((uint64_t)palette_buffer_view, VK_OBJECT_TYPE_BUFFER_VIEW, "Palette colors");
	}

	{
		const int nodes_size = num_nodes * sizeof (palette_octree_node_t);

		// Allocate palette buffer & upload to GPU
		VkBufferCreateInfo buffer_create_info;
		memset (&buffer_create_info, 0, sizeof (buffer_create_info));
		buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		buffer_create_info.size = nodes_size;
		buffer_create_info.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		VkResult err = vkCreateBuffer (vulkan_globals.device, &buffer_create_info, NULL, &palette_octree_buffer);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateBuffer failed");

		GL_SetObjectName ((uint64_t)palette_octree_buffer, VK_OBJECT_TYPE_BUFFER, "Palette octree");

		VkMemoryRequirements memory_requirements;
		vkGetBufferMemoryRequirements (vulkan_globals.device, palette_octree_buffer, &memory_requirements);

		VkMemoryAllocateInfo memory_allocate_info;
		memset (&memory_allocate_info, 0, sizeof (memory_allocate_info));
		memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		memory_allocate_info.allocationSize = memory_requirements.size;
		memory_allocate_info.memoryTypeIndex = GL_MemoryTypeFromProperties (memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);

		Atomic_IncrementUInt32 (&num_vulkan_misc_allocations);
		vulkan_memory_t memory;
		R_AllocateVulkanMemory (&memory, &memory_allocate_info, VULKAN_MEMORY_TYPE_HOST);
		GL_SetObjectName ((uint64_t)memory.handle, VK_OBJECT_TYPE_DEVICE_MEMORY, "Palette octree");

		err = vkBindBufferMemory (vulkan_globals.device, palette_octree_buffer, memory.handle, 0);
		if (err != VK_SUCCESS)
			Sys_Error ("vkBindBufferMemory failed");

		VkBuffer		staging_buffer;
		VkCommandBuffer command_buffer;
		int				staging_offset;
		uint32_t	   *staging_memory = (uint32_t *)R_StagingAllocate (nodes_size, 1, &command_buffer, &staging_buffer, &staging_offset);

		VkBufferCopy region;
		region.srcOffset = staging_offset;
		region.dstOffset = 0;
		region.size = nodes_size;
		vkCmdCopyBuffer (command_buffer, staging_buffer, palette_octree_buffer, 1, &region);

		R_StagingBeginCopy ();
		memcpy (staging_memory, nodes, nodes_size);
		R_StagingEndCopy ();
	}
}

/*
===================
VID_Init
===================
*/
void VID_Init (void)
{
	static char vid_center[] = "SDL_VIDEO_CENTERED=center";
	int			p, width, height, refreshrate;
	int			display_width, display_height, display_refreshrate;
	qboolean	fullscreen;
	const char *read_vars[] = {"vid_fullscreen",		"vid_width",	"vid_height", "vid_refreshrate", "vid_vsync",
							   "vid_desktopfullscreen", "vid_fsaamode", "vid_fsaa",	  "vid_borderless"};
#define num_readvars (sizeof (read_vars) / sizeof (read_vars[0]))

	Cvar_RegisterVariable (&vid_fullscreen);  // johnfitz
	Cvar_RegisterVariable (&vid_width);		  // johnfitz
	Cvar_RegisterVariable (&vid_height);	  // johnfitz
	Cvar_RegisterVariable (&vid_refreshrate); // johnfitz
	Cvar_RegisterVariable (&vid_vsync);		  // johnfitz
	Cvar_RegisterVariable (&vid_filter);
	Cvar_RegisterVariable (&vid_anisotropic);
	Cvar_RegisterVariable (&vid_fsaamode);
	Cvar_RegisterVariable (&vid_fsaa);
	Cvar_RegisterVariable (&vid_desktopfullscreen); // QuakeSpasm
	Cvar_RegisterVariable (&vid_borderless);		// QuakeSpasm
	Cvar_RegisterVariable (&vid_palettize);
	Cvar_SetCallback (&vid_fullscreen, VID_Changed_f);
	Cvar_SetCallback (&vid_width, VID_Changed_f);
	Cvar_SetCallback (&vid_height, VID_Changed_f);
	Cvar_SetCallback (&vid_refreshrate, VID_Changed_f);
	Cvar_SetCallback (&vid_filter, VID_FilterChanged_f);
	Cvar_SetCallback (&vid_anisotropic, VID_FilterChanged_f);
	Cvar_SetCallback (&vid_fsaamode, VID_Changed_f);
	Cvar_SetCallback (&vid_fsaa, VID_Changed_f);
	Cvar_SetCallback (&vid_vsync, VID_Changed_f);
	Cvar_SetCallback (&vid_desktopfullscreen, VID_Changed_f);
	Cvar_SetCallback (&vid_borderless, VID_Changed_f);

	Cmd_AddCommand ("vid_unlock", VID_Unlock);	   // johnfitz
	Cmd_AddCommand ("vid_restart", VID_Restart_f); // johnfitz
	Cmd_AddCommand ("vid_test", VID_Test);		   // johnfitz
	Cmd_AddCommand ("vid_describecurrentmode", VID_DescribeCurrentMode_f);
	Cmd_AddCommand ("vid_describemodes", VID_DescribeModes_f);

#ifdef _DEBUG
	Cmd_AddCommand ("create_palette_octree", CreatePaletteOctree_f);
#endif

	putenv (vid_center); /* SDL_putenv is problematic in versions <= 1.2.9 */

	if (SDL_InitSubSystem (SDL_INIT_VIDEO) < 0)
		Sys_Error ("Couldn't init SDL video: %s", SDL_GetError ());

	{
		SDL_DisplayMode mode;
		if (SDL_GetDesktopDisplayMode (0, &mode) != 0)
			Sys_Error ("Could not get desktop display mode: %s\n", SDL_GetError ());

		display_width = mode.w;
		display_height = mode.h;
		display_refreshrate = mode.refresh_rate;
	}

	if (CFG_OpenConfig ("config.cfg") == 0)
	{
		CFG_ReadCvars (read_vars, num_readvars);
		CFG_CloseConfig ();
	}
	CFG_ReadCvarOverrides (read_vars, num_readvars);

	VID_InitModelist ();

	width = (int)vid_width.value;
	height = (int)vid_height.value;
	refreshrate = (int)vid_refreshrate.value;
	fullscreen = (int)vid_fullscreen.value;
	vulkan_globals.want_full_screen_exclusive = vid_fullscreen.value >= 2;

	if (COM_CheckParm ("-current"))
	{
		width = display_width;
		height = display_height;
		refreshrate = display_refreshrate;
		fullscreen = true;
	}
	else
	{
		p = COM_CheckParm ("-width");
		if (p && p < com_argc - 1)
		{
			width = atoi (com_argv[p + 1]);

			if (!COM_CheckParm ("-height"))
				height = width * 3 / 4;
		}

		p = COM_CheckParm ("-height");
		if (p && p < com_argc - 1)
		{
			height = atoi (com_argv[p + 1]);

			if (!COM_CheckParm ("-width"))
				width = height * 4 / 3;
		}

		p = COM_CheckParm ("-refreshrate");
		if (p && p < com_argc - 1)
			refreshrate = atoi (com_argv[p + 1]);

		if (COM_CheckParm ("-window") || COM_CheckParm ("-w"))
			fullscreen = false;
		else if (COM_CheckParm ("-fullscreen") || COM_CheckParm ("-f"))
			fullscreen = true;
	}

	if (!VID_ValidMode (width, height, refreshrate, fullscreen))
	{
		width = (int)vid_width.value;
		height = (int)vid_height.value;
		refreshrate = (int)vid_refreshrate.value;
		fullscreen = (int)vid_fullscreen.value;
	}

	if (!VID_ValidMode (width, height, refreshrate, fullscreen))
	{
		width = 640;
		height = 480;
		refreshrate = display_refreshrate;
		fullscreen = false;
	}

	vid_initialized = true;

	vid.colormap = host_colormap;
	vid.fullbright = 256 - LittleLong (*((int *)vid.colormap + 2048));

	VID_SetMode (width, height, refreshrate, fullscreen);

	// set window icon
	PL_SetWindowIcon ();

	Con_Printf ("\nVulkan Initialization\n");
	SDL_Vulkan_LoadLibrary (NULL);
	GL_InitInstance ();
	GL_InitDevice ();
	GL_InitCommandBuffers ();
	vulkan_globals.staging_buffer_size = INITIAL_STAGING_BUFFER_SIZE_KB * 1024;
	R_InitStagingBuffers ();
	R_CreateDescriptorSetLayouts ();
	R_CreateDescriptorPool ();
	R_InitGPUBuffers ();
	R_InitSamplers ();
	R_CreatePipelineLayouts ();
	R_CreatePaletteOctreeBuffers (palette_octree_colors, NUM_PALETTE_OCTREE_COLORS, palette_octree_nodes, NUM_PALETTE_OCTREE_NODES);
	// GL_CreateRenderResources ();

	// johnfitz -- removed code creating "glquake" subdirectory

	vid_menucmdfn = VID_Menu_f; // johnfitz
	vid_menudrawfn = VID_MenuDraw;
	vid_menukeyfn = VID_MenuKey;

	VID_Gamma_Init (); // johnfitz
	VID_Menu_Init ();  // johnfitz

	// QuakeSpasm: current vid settings should override config file settings.
	// so we have to lock the vid mode from now until after all config files are read.
	vid_locked = true;
}

/*
===================
VID_Restart
===================
*/
static void VID_Restart (qboolean set_mode)
{
	GL_SynchronizeEndRenderingTask ();

	int		 width, height, refreshrate;
	qboolean fullscreen;

	width = (int)vid_width.value;
	height = (int)vid_height.value;
	refreshrate = (int)vid_refreshrate.value;
	fullscreen = vid_fullscreen.value ? true : false;
	vulkan_globals.want_full_screen_exclusive = vid_fullscreen.value >= 2;

	//
	// validate new mode
	//
	if (set_mode && !VID_ValidMode (width, height, refreshrate, fullscreen))
	{
		Con_Printf ("%dx%d %dHz %s is not a valid mode\n", width, height, refreshrate, fullscreen ? "fullscreen" : "windowed");
		return;
	}

	scr_initialized = false;

	GL_WaitForDeviceIdle ();
	GL_DestroyRenderResources ();

	//
	// set new mode
	//
	if (set_mode)
		VID_SetMode (width, height, refreshrate, fullscreen);

	GL_CreateRenderResources ();

	// conwidth and conheight need to be recalculated
	vid.conwidth = (scr_conwidth.value > 0) ? (int)scr_conwidth.value : (scr_conscale.value > 0) ? (int)(vid.width / scr_conscale.value) : vid.width;
	vid.conwidth = CLAMP (320, vid.conwidth, vid.width);
	vid.conwidth &= 0xFFFFFFF8;
	vid.conheight = vid.conwidth * vid.height / vid.width;
	//
	// keep cvars in line with actual mode
	//
	VID_SyncCvars ();

	//
	// update mouse grab
	//
	if (key_dest == key_console || key_dest == key_menu)
	{
		if (modestate == MS_WINDOWED)
			IN_Deactivate (false);
		else if (modestate == MS_FULLSCREEN)
			IN_HideCursor ();
	}

	R_InitSamplers ();

	SCR_UpdateRelativeScale ();

	scr_initialized = true;
}

/*
===================
VID_Restart_f -- johnfitz -- change video modes on the fly
===================
*/
static void VID_Restart_f (void)
{
	if (vid_locked || !vid_changed)
		return;
	VID_Restart (true);
}

/*
===================
VID_Toggle
new proc by S.A., called by alt-return key binding.
===================
*/
void VID_Toggle (void)
{
	qboolean toggleWorked;
	Uint32	 flags = 0;

	S_ClearBuffer ();

	if (!VID_GetFullscreen ())
	{
		flags = vid_desktopfullscreen.value ? SDL_WINDOW_FULLSCREEN_DESKTOP : SDL_WINDOW_FULLSCREEN;
	}

	toggleWorked = SDL_SetWindowFullscreen (draw_context, flags) == 0;
	if (toggleWorked)
	{
		modestate = VID_GetFullscreen () ? MS_FULLSCREEN : MS_WINDOWED;

		VID_SyncCvars ();

		// update mouse grab
		if (key_dest == key_console || key_dest == key_menu)
		{
			if (modestate == MS_WINDOWED)
				IN_Deactivate (false);
			else if (modestate == MS_FULLSCREEN)
				IN_HideCursor ();
		}
	}
}

// For settings that are not applied during vid_restart
typedef struct
{
	int host_maxfps;
	int r_waterwarp;
	int r_particles;
	int r_scale;
	int vid_filter;
	int vid_anisotropic;
	int vid_palettize;
} vid_menu_settings_t;

static vid_menu_settings_t menu_settings;

/*
================
VID_SyncCvars -- johnfitz -- set vid cvars to match current video mode
================
*/
void VID_SyncCvars (void)
{
	if (draw_context)
	{
		if (!VID_GetDesktopFullscreen ())
		{
			Cvar_SetValueQuick (&vid_width, VID_GetCurrentWidth ());
			Cvar_SetValueQuick (&vid_height, VID_GetCurrentHeight ());
		}
		Cvar_SetValueQuick (&vid_refreshrate, VID_GetCurrentRefreshRate ());
		Cvar_SetQuick (&vid_fullscreen, VID_GetFullscreen () ? (vulkan_globals.want_full_screen_exclusive ? "2" : "1") : "0");
		// don't sync vid_desktopfullscreen, it's a user preference that
		// should persist even if we are in windowed mode.
	}

	menu_settings.host_maxfps = CLAMP (0, host_maxfps.value, 1000);
	menu_settings.r_waterwarp = CLAMP (0, (int)r_waterwarp.value, 2);
	menu_settings.r_particles = CLAMP (0, (int)r_particles.value, 2);
	menu_settings.r_scale = CLAMP (1, (int)r_scale.value, 8);
	menu_settings.vid_filter = CLAMP (0, (int)vid_filter.value, 1);
	menu_settings.vid_anisotropic = CLAMP (0, (int)vid_anisotropic.value, 1);
	menu_settings.vid_palettize = CLAMP (0, (int)vid_palettize.value, 1);

	vid_changed = false;
}

//==========================================================================
//
//  NEW VIDEO MENU -- johnfitz
//
//==========================================================================

enum
{
	VID_OPT_MODE,
	VID_OPT_BPP,
	VID_OPT_REFRESHRATE,
	VID_OPT_FULLSCREEN,
	VID_OPT_VSYNC,
	VID_OPT_MAX_FPS,
	VID_OPT_ANTIALIASING_SAMPLES,
	VID_OPT_ANTIALIASING_MODE,
	VID_OPT_RENDER_SCALE,
	VID_OPT_FILTER,
	VID_OPT_ANISOTROPY,
	VID_OPT_UNDERWATER,
	VID_OPT_PARTICLES,
	VID_OPT_TEST,
	VID_OPT_APPLY,
	VIDEO_OPTIONS_ITEMS
};

static int video_options_cursor = 0;

typedef struct
{
	int width, height;
} vid_menu_mode;

// TODO: replace these fixed-length arrays with hunk_allocated buffers
static vid_menu_mode vid_menu_modes[MAX_MODE_LIST];
static int			 vid_menu_nummodes = 0;

static int vid_menu_rates[MAX_RATES_LIST];
static int vid_menu_numrates = 0;

/*
================
VID_Menu_Init
================
*/
static void VID_Menu_Init (void)
{
	int i, j, h, w;

	for (i = 0; i < nummodes; i++)
	{
		w = modelist[i].width;
		h = modelist[i].height;

		for (j = 0; j < vid_menu_nummodes; j++)
		{
			if (vid_menu_modes[j].width == w && vid_menu_modes[j].height == h)
				break;
		}

		if (j == vid_menu_nummodes)
		{
			vid_menu_modes[j].width = w;
			vid_menu_modes[j].height = h;
			vid_menu_nummodes++;
		}
	}
}

/*
================
VID_Menu_RebuildRateList

regenerates rate list based on current vid_width, vid_height
================
*/
static void VID_Menu_RebuildRateList (void)
{
	int i, j, r;

	vid_menu_numrates = 0;

	for (i = 0; i < nummodes; i++)
	{
		// rate list is limited to rates available with current width/height
		if (modelist[i].width != vid_width.value || modelist[i].height != vid_height.value)
			continue;

		r = modelist[i].refreshrate;

		for (j = 0; j < vid_menu_numrates; j++)
		{
			if (vid_menu_rates[j] == r)
				break;
		}

		if (j == vid_menu_numrates)
		{
			vid_menu_rates[j] = r;
			vid_menu_numrates++;
		}
	}

	// if there are no valid fullscreen refreshrates for this width/height, just pick one
	if (vid_menu_numrates == 0)
	{
		Cvar_SetValue ("vid_refreshrate", (float)modelist[0].refreshrate);
		return;
	}

	// if vid_refreshrate is not in the new list, change vid_refreshrate
	for (i = 0; i < vid_menu_numrates; i++)
		if (vid_menu_rates[i] == (int)(vid_refreshrate.value))
			break;

	if (i == vid_menu_numrates)
		Cvar_SetValue ("vid_refreshrate", (float)vid_menu_rates[0]);
}

/*
================
VID_Menu_ChooseNextMode

chooses next resolution in order, then updates vid_width and
vid_height cvars, then updates refreshrate lists
================
*/
static void VID_Menu_ChooseNextMode (int dir)
{
	int i;

	if (vid_menu_nummodes)
	{
		for (i = 0; i < vid_menu_nummodes; i++)
		{
			if (vid_menu_modes[i].width == vid_width.value && vid_menu_modes[i].height == vid_height.value)
				break;
		}

		if (i == vid_menu_nummodes) // can't find it in list, so it must be a custom windowed res
		{
			i = 0;
		}
		else
		{
			i += dir;
			if (i >= vid_menu_nummodes)
				i = 0;
			else if (i < 0)
				i = vid_menu_nummodes - 1;
		}

		Cvar_SetValueQuick (&vid_width, (float)vid_menu_modes[i].width);
		Cvar_SetValueQuick (&vid_height, (float)vid_menu_modes[i].height);
		VID_Menu_RebuildRateList ();
	}
}

/*
================
VID_Menu_ChooseNextBpp
================
*/
static void VID_Menu_ChooseNextBpp (void)
{
	menu_settings.vid_palettize = (menu_settings.vid_palettize + 1) % 2;
}

/*
================
VID_Menu_ChooseNextAAMode
================
*/
static void VID_Menu_ChooseNextAAMode (int dir)
{
	if (vulkan_globals.supersampling)
		Cvar_SetValueQuick (&vid_fsaamode, (float)(((int)vid_fsaamode.value + 2 + dir) % 2));
}

/*
================
VID_Menu_ChooseNextAASamples
================
*/
static void VID_Menu_ChooseNextAASamples (int dir)
{
	int value = vid_fsaa.value;

	if (dir > 0)
	{
		if (value >= 16)
			value = 0;
		else if (value >= 8)
			value = 16;
		else if (value >= 4)
			value = 8;
		else if (value >= 2)
			value = 4;
		else
			value = 2;
	}
	else
	{
		if (value <= 0)
			value = 16;
		else if (value <= 2)
			value = 0;
		else if (value <= 4)
			value = 2;
		else if (value <= 8)
			value = 4;
		else if (value <= 16)
			value = 8;
		else
			value = 16;
	}

	Cvar_SetValueQuick (&vid_fsaa, (float)value);
}

/*
================
VID_Menu_ChooseNextRenderScale
================
*/
static void VID_Menu_ChooseNextRenderScale (int dir)
{
	int value = menu_settings.r_scale;

	if (dir > 0)
	{
		if (value >= 8)
			value = 0;
		else if (value >= 4)
			value = 8;
		else if (value >= 2)
			value = 4;
		else
			value = 2;
	}
	else
	{
		if (value <= 0)
			value = 8;
		else if (value <= 2)
			value = 0;
		else if (value <= 4)
			value = 2;
		else if (value <= 8)
			value = 4;
		else
			value = 8;
	}

	menu_settings.r_scale = value;
}

/*
================
VID_Menu_ChooseNextMaxFPS
================
*/
static void VID_Menu_ChooseNextMaxFPS (int dir)
{
	menu_settings.host_maxfps = CLAMP (0, ((menu_settings.host_maxfps + (dir * 10)) / 10) * 10, 1000);
}

/*
================
VID_Menu_ChooseNextWaterWarp
================
*/
static void VID_Menu_ChooseNextWaterWarp (int dir)
{
	menu_settings.r_waterwarp = (menu_settings.r_waterwarp + 3 + dir) % 3;
}

/*
================
VID_Menu_ChooseNextParticles
================
*/
static void VID_Menu_ChooseNextParticles (int dir)
{
	if (dir > 0)
	{
		if (menu_settings.r_particles == 0)
			menu_settings.r_particles = 2;
		else if (menu_settings.r_particles == 2)
			menu_settings.r_particles = 1;
		else
			menu_settings.r_particles = 0;
	}
	else
	{
		if (menu_settings.r_particles == 0)
			menu_settings.r_particles = 1;
		else if (menu_settings.r_particles == 2)
			menu_settings.r_particles = 0;
		else
			menu_settings.r_particles = 2;
	}
}

/*
================
VID_Menu_ChooseNextRate

chooses next refresh rate in order, then updates vid_refreshrate cvar
================
*/
static void VID_Menu_ChooseNextRate (int dir)
{
	int i;

	for (i = 0; i < vid_menu_numrates; i++)
	{
		if (vid_menu_rates[i] == vid_refreshrate.value)
			break;
	}

	if (i == vid_menu_numrates) // can't find it in list
	{
		i = 0;
	}
	else
	{
		i += dir;
		if (i >= vid_menu_numrates)
			i = 0;
		else if (i < 0)
			i = vid_menu_numrates - 1;
	}

	Cvar_SetValue ("vid_refreshrate", (float)vid_menu_rates[i]);
}

/*
================
VID_Menu_ChooseNextFullScreenMode
================
*/
static void VID_Menu_ChooseNextFullScreenMode (int dir)
{
	if (vulkan_globals.full_screen_exclusive)
		Cvar_SetValueQuick (&vid_fullscreen, (float)(((int)vid_fullscreen.value + 3 + dir) % 3));
	else
		Cvar_SetValueQuick (&vid_fullscreen, (float)(((int)vid_fullscreen.value + 2 + dir) % 2));
}

/*
================
VID_Menu_ChooseNextVSyncMode
================
*/
static void VID_Menu_ChooseNextVSyncMode (int dir)
{
	Cvar_SetValueQuick (&vid_vsync, (float)(((int)vid_vsync.value + 3 + dir) % 3));
}

/*
================
VID_MenuKey
================
*/
static void VID_MenuKey (int key)
{
	switch (key)
	{
	case K_MOUSE2:
	case K_ESCAPE:
		VID_SyncCvars (); // sync cvars before leaving menu. FIXME: there are other ways to leave menu
		S_LocalSound ("misc/menu1.wav");
		M_Menu_Options_f ();
		break;

	case K_UPARROW:
		S_LocalSound ("misc/menu1.wav");
		video_options_cursor--;
		if (video_options_cursor < 0)
			video_options_cursor = VIDEO_OPTIONS_ITEMS - 1;
		break;

	case K_DOWNARROW:
		S_LocalSound ("misc/menu1.wav");
		video_options_cursor++;
		if (video_options_cursor >= VIDEO_OPTIONS_ITEMS)
			video_options_cursor = 0;
		break;

	case K_LEFTARROW:
		S_LocalSound ("misc/menu3.wav");
		switch (video_options_cursor)
		{
		case VID_OPT_MODE:
			VID_Menu_ChooseNextMode (1);
			break;
		case VID_OPT_BPP:
			VID_Menu_ChooseNextBpp ();
			break;
		case VID_OPT_REFRESHRATE:
			VID_Menu_ChooseNextRate (1);
			break;
		case VID_OPT_FULLSCREEN:
			VID_Menu_ChooseNextFullScreenMode (-1);
			break;
		case VID_OPT_VSYNC:
			VID_Menu_ChooseNextVSyncMode (-1);
			break;
		case VID_OPT_MAX_FPS:
			VID_Menu_ChooseNextMaxFPS (-1);
			break;
		case VID_OPT_ANTIALIASING_SAMPLES:
			VID_Menu_ChooseNextAASamples (-1);
			break;
		case VID_OPT_ANTIALIASING_MODE:
			VID_Menu_ChooseNextAAMode (-1);
			break;
		case VID_OPT_RENDER_SCALE:
			VID_Menu_ChooseNextRenderScale (-1);
			break;
		case VID_OPT_FILTER:
			menu_settings.vid_filter = (menu_settings.vid_filter == 0) ? 1 : 0;
			break;
		case VID_OPT_ANISOTROPY:
			menu_settings.vid_anisotropic = (menu_settings.vid_anisotropic == 0) ? 1 : 0;
			break;
		case VID_OPT_UNDERWATER:
			VID_Menu_ChooseNextWaterWarp (-1);
			break;
		case VID_OPT_PARTICLES:
			VID_Menu_ChooseNextParticles (-1);
			break;
		default:
			break;
		}
		break;

	case K_RIGHTARROW:
		S_LocalSound ("misc/menu3.wav");
		switch (video_options_cursor)
		{
		case VID_OPT_MODE:
			VID_Menu_ChooseNextMode (-1);
			break;
		case VID_OPT_BPP:
			VID_Menu_ChooseNextBpp ();
			break;
		case VID_OPT_REFRESHRATE:
			VID_Menu_ChooseNextRate (-1);
			break;
		case VID_OPT_FULLSCREEN:
			VID_Menu_ChooseNextFullScreenMode (1);
			break;
		case VID_OPT_VSYNC:
			VID_Menu_ChooseNextVSyncMode (1);
			break;
		case VID_OPT_MAX_FPS:
			VID_Menu_ChooseNextMaxFPS (1);
			break;
		case VID_OPT_ANTIALIASING_SAMPLES:
			VID_Menu_ChooseNextAASamples (1);
			break;
		case VID_OPT_ANTIALIASING_MODE:
			VID_Menu_ChooseNextAAMode (1);
			break;
		case VID_OPT_RENDER_SCALE:
			VID_Menu_ChooseNextRenderScale (1);
			break;
		case VID_OPT_FILTER:
			menu_settings.vid_filter = (menu_settings.vid_filter == 0) ? 1 : 0;
			break;
		case VID_OPT_ANISOTROPY:
			menu_settings.vid_anisotropic = (menu_settings.vid_anisotropic == 0) ? 1 : 0;
			break;
		case VID_OPT_UNDERWATER:
			VID_Menu_ChooseNextWaterWarp (1);
			break;
		case VID_OPT_PARTICLES:
			VID_Menu_ChooseNextParticles (1);
			break;
		default:
			break;
		}
		break;

	case K_MOUSE1:
	case K_ENTER:
	case K_KP_ENTER:
		m_entersound = true;
		switch (video_options_cursor)
		{
		case VID_OPT_MODE:
			VID_Menu_ChooseNextMode (-1);
			break;
		case VID_OPT_BPP:
			VID_Menu_ChooseNextBpp ();
			break;
		case VID_OPT_REFRESHRATE:
			VID_Menu_ChooseNextRate (-1);
			break;
		case VID_OPT_FULLSCREEN:
			VID_Menu_ChooseNextFullScreenMode (1);
			break;
		case VID_OPT_VSYNC:
			VID_Menu_ChooseNextVSyncMode (1);
			break;
		case VID_OPT_MAX_FPS:
			VID_Menu_ChooseNextMaxFPS (1);
			break;
		case VID_OPT_ANTIALIASING_SAMPLES:
			VID_Menu_ChooseNextAASamples (1);
			break;
		case VID_OPT_ANTIALIASING_MODE:
			VID_Menu_ChooseNextAAMode (1);
			break;
		case VID_OPT_RENDER_SCALE:
			VID_Menu_ChooseNextRenderScale (1);
			break;
		case VID_OPT_FILTER:
			menu_settings.vid_filter = (menu_settings.vid_filter == 0) ? 1 : 0;
			break;
		case VID_OPT_ANISOTROPY:
			menu_settings.vid_anisotropic = (menu_settings.vid_anisotropic == 0) ? 1 : 0;
			break;
		case VID_OPT_UNDERWATER:
			VID_Menu_ChooseNextWaterWarp (1);
			break;
		case VID_OPT_PARTICLES:
			VID_Menu_ChooseNextParticles (1);
			break;
		case VID_OPT_TEST:
			Cbuf_AddText ("vid_test\n");
			break;
		case VID_OPT_APPLY:
			Cvar_SetValueQuick (&host_maxfps, menu_settings.host_maxfps);
			Cvar_SetValueQuick (&r_particles, menu_settings.r_particles);
			Cvar_SetValueQuick (&r_waterwarp, menu_settings.r_waterwarp);
			Cvar_SetValueQuick (&r_scale, menu_settings.r_scale);
			Cvar_SetValueQuick (&vid_filter, menu_settings.vid_filter);
			Cvar_SetValueQuick (&vid_anisotropic, menu_settings.vid_anisotropic);
			Cvar_SetValueQuick (&vid_palettize, menu_settings.vid_palettize);
			Cbuf_AddText ("vid_restart\n");
			key_dest = key_game;
			m_state = m_none;
			IN_Activate ();
			break;
		default:
			break;
		}
		break;

	default:
		break;
	}
}

/*
================
VID_MenuDraw
================
*/
static void VID_MenuDraw (cb_context_t *cbx)
{
	int			i, y;
	qpic_t	   *p;
	const char *title;

	y = 4;

	// plaque
	p = Draw_CachePic ("gfx/qplaque.lmp");
	M_DrawTransPic (cbx, 16, y, p);

	// p = Draw_CachePic ("gfx/vidmodes.lmp");
	p = Draw_CachePic ("gfx/p_option.lmp");
	M_DrawPic (cbx, (320 - p->width) / 2, y, p);

	y += 28;

	// title
	title = "Video Options";
	M_PrintWhite (cbx, (320 - 8 * strlen (title)) / 2, y, title);

	y += 16;

	// options
	for (i = 0; i < VIDEO_OPTIONS_ITEMS; i++)
	{
		switch (i)
		{
		case VID_OPT_MODE:
			M_Print (cbx, 16, y, "        Video mode");
			M_Print (cbx, 184, y, va ("%ix%i", (int)vid_width.value, (int)vid_height.value));
			break;
		case VID_OPT_BPP:
			M_Print (cbx, 16, y, "       Color depth");
			M_Print (cbx, 184, y, (menu_settings.vid_palettize == 1) ? "classic" : "modern");
			break;
		case VID_OPT_REFRESHRATE:
			M_Print (cbx, 16, y, "      Refresh rate");
			M_Print (cbx, 184, y, va ("%i", (int)vid_refreshrate.value));
			break;
		case VID_OPT_FULLSCREEN:
			M_Print (cbx, 16, y, "        Fullscreen");
			M_Print (cbx, 184, y, ((int)vid_fullscreen.value == 0) ? "off" : (((int)vid_fullscreen.value == 1) ? "on" : "exclusive"));
			break;
		case VID_OPT_VSYNC:
			M_Print (cbx, 16, y, "     Vertical sync");
			M_Print (cbx, 184, y, ((int)vid_vsync.value == 0) ? "off" : (((int)vid_vsync.value == 1) ? "on" : "triple buffer"));
			break;
		case VID_OPT_MAX_FPS:
			M_Print (cbx, 16, y, "           Max FPS");
			if (menu_settings.host_maxfps <= 0)
				M_Print (cbx, 184, y, "no limit");
			else
				M_Print (cbx, 184, y, va ("%d", menu_settings.host_maxfps));
			break;
		case VID_OPT_ANTIALIASING_SAMPLES:
			M_Print (cbx, 16, y, "      Antialiasing");
			M_Print (cbx, 184, y, ((int)vid_fsaa.value >= 2) ? va ("%ix", CLAMP (2, (int)vid_fsaa.value, 16)) : "off");
			break;
		case VID_OPT_ANTIALIASING_MODE:
			M_Print (cbx, 16, y, "           AA Mode");
			M_Print (cbx, 184, y, ((int)vid_fsaamode.value == 0) ? "Multisample" : "Supersample");
			break;
		case VID_OPT_RENDER_SCALE:
			M_Print (cbx, 16, y, "      Render Scale");
			M_Print (cbx, 184, y, (menu_settings.r_scale >= 2) ? va ("1/%i", menu_settings.r_scale) : "off");
			break;
		case VID_OPT_FILTER:
			M_Print (cbx, 16, y, "          Textures");
			M_Print (cbx, 184, y, (menu_settings.vid_filter == 0) ? "smooth" : "classic");
			break;
		case VID_OPT_ANISOTROPY:
			M_Print (cbx, 16, y, "       Anisotropic");
			M_Print (cbx, 184, y, (menu_settings.vid_anisotropic == 0) ? "off" : va ("on (%gx)", vulkan_globals.device_properties.limits.maxSamplerAnisotropy));
			break;
		case VID_OPT_UNDERWATER:
			M_Print (cbx, 16, y, "     Underwater FX");
			M_Print (cbx, 184, y, (menu_settings.r_waterwarp == 0) ? "off" : ((menu_settings.r_waterwarp == 1) ? "Classic" : "glQuake"));
			break;
		case VID_OPT_PARTICLES:
			M_Print (cbx, 16, y, "         Particles");
			M_Print (cbx, 184, y, (menu_settings.r_particles == 0) ? "off" : ((menu_settings.r_particles == 2) ? "Classic" : "glQuake"));
			break;
		case VID_OPT_TEST:
			y += 8; // separate the test and apply items
			M_Print (cbx, 16, y, "      Test changes");
			break;
		case VID_OPT_APPLY:
			M_Print (cbx, 16, y, "     Apply changes");
			break;
		}

		M_Mouse_UpdateCursor (&video_options_cursor, 12, 400, y, 8, i);
		if (video_options_cursor == i)
			M_DrawCharacter (cbx, 168, y, 12 + ((int)(realtime * 4) & 1));

		y += 8;
	}
}

/*
================
VID_Menu_f
================
*/
static void VID_Menu_f (void)
{
	IN_Deactivate (modestate == MS_WINDOWED);
	key_dest = key_menu;
	m_state = m_video;
	m_entersound = true;

	// set all the cvars to match the current mode when entering the menu
	VID_SyncCvars ();

	// set up bpp and rate lists based on current cvars
	VID_Menu_RebuildRateList ();
}

/*
==============================================================================

SCREEN SHOTS

==============================================================================
*/

static void SCR_ScreenShot_Usage (void)
{
	Con_Printf ("usage: screenshot <format> <quality>\n");
	Con_Printf ("   format must be \"png\" or \"tga\" or \"jpg\"\n");
	Con_Printf ("   quality must be 1-100\n");
	return;
}

/*
==================
SCR_ScreenShot_f -- johnfitz -- rewritten to use Image_WriteTGA
==================
*/
void SCR_ScreenShot_f (void)
{
	VkBuffer buffer;
	VkResult err;
	char	 ext[4];
	char	 imagename[16]; // johnfitz -- was [80]
	char	 checkname[MAX_OSPATH];
	int		 i, quality;
	qboolean ok;

	qboolean bgra = (vulkan_globals.swap_chain_format == VK_FORMAT_B8G8R8A8_UNORM) || (vulkan_globals.swap_chain_format == VK_FORMAT_B8G8R8A8_SRGB);

	memcpy (ext, "png", sizeof (ext));

	if (Cmd_Argc () >= 2)
	{
		const char *requested_ext = Cmd_Argv (1);

		if (!q_strcasecmp ("png", requested_ext) || !q_strcasecmp ("tga", requested_ext) || !q_strcasecmp ("jpg", requested_ext))
			memcpy (ext, requested_ext, sizeof (ext));
		else
		{
			SCR_ScreenShot_Usage ();
			return;
		}
	}

	// read quality as the 3rd param (only used for JPG)
	quality = 90;
	if (Cmd_Argc () >= 3)
		quality = atoi (Cmd_Argv (2));
	if (quality < 1 || quality > 100)
	{
		SCR_ScreenShot_Usage ();
		return;
	}

	if ((vulkan_globals.swap_chain_format != VK_FORMAT_B8G8R8A8_UNORM) && (vulkan_globals.swap_chain_format != VK_FORMAT_B8G8R8A8_SRGB) &&
		(vulkan_globals.swap_chain_format != VK_FORMAT_R8G8B8A8_UNORM) && (vulkan_globals.swap_chain_format != VK_FORMAT_R8G8B8A8_SRGB))
	{
		Con_Printf ("SCR_ScreenShot_f: Unsupported surface format\n");
		return;
	}

	// find a file name to save it to
	for (i = 0; i < 10000; i++)
	{
		q_snprintf (imagename, sizeof (imagename), "vkquake%04i.%s", i, ext); // "fitz%04cbx_index.tga"
		q_snprintf (checkname, sizeof (checkname), "%s/%s", com_gamedir, imagename);
		if (Sys_FileType (checkname) == FS_ENT_NONE)
			break; // file doesn't exist
	}
	if (i == 10000)
	{
		Con_Printf ("SCR_ScreenShot_f: Couldn't find an unused filename\n");
		return;
	}

	// get data
	VkBufferCreateInfo buffer_create_info;
	memset (&buffer_create_info, 0, sizeof (buffer_create_info));
	buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	buffer_create_info.size = glwidth * glheight * 4;
	buffer_create_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	err = vkCreateBuffer (vulkan_globals.device, &buffer_create_info, NULL, &buffer);
	if (err != VK_SUCCESS)
		Sys_Error ("vkCreateBuffer failed");

	VkMemoryRequirements memory_requirements;
	vkGetBufferMemoryRequirements (vulkan_globals.device, buffer, &memory_requirements);

	uint32_t memory_type_index = GL_MemoryTypeFromProperties (
		memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, VK_MEMORY_PROPERTY_HOST_CACHED_BIT);

	VkMemoryAllocateInfo memory_allocate_info;
	memset (&memory_allocate_info, 0, sizeof (memory_allocate_info));
	memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	memory_allocate_info.allocationSize = memory_requirements.size;
	memory_allocate_info.memoryTypeIndex = memory_type_index;

	VkDeviceMemory memory;
	err = vkAllocateMemory (vulkan_globals.device, &memory_allocate_info, NULL, &memory);
	if (err != VK_SUCCESS)
		Sys_Error ("vkAllocateMemory failed");

	err = vkBindBufferMemory (vulkan_globals.device, buffer, memory, 0);
	if (err != VK_SUCCESS)
		Sys_Error ("vkBindBufferMemory failed");

	VkCommandBuffer command_buffer;

	VkCommandBufferAllocateInfo command_buffer_allocate_info;
	memset (&command_buffer_allocate_info, 0, sizeof (command_buffer_allocate_info));
	command_buffer_allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	command_buffer_allocate_info.commandPool = transient_command_pool;
	command_buffer_allocate_info.commandBufferCount = 1;
	err = vkAllocateCommandBuffers (vulkan_globals.device, &command_buffer_allocate_info, &command_buffer);
	if (err != VK_SUCCESS)
		Sys_Error ("vkAllocateCommandBuffers failed");

	VkCommandBufferBeginInfo command_buffer_begin_info;
	memset (&command_buffer_begin_info, 0, sizeof (command_buffer_begin_info));
	command_buffer_begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	command_buffer_begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	err = vkBeginCommandBuffer (command_buffer, &command_buffer_begin_info);
	if (err != VK_SUCCESS)
		Sys_Error ("vkBeginCommandBuffer failed");

	VkImageMemoryBarrier image_barrier;
	image_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	image_barrier.pNext = NULL;
	image_barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	image_barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	image_barrier.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	image_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	image_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	image_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	image_barrier.image = swapchain_images[current_cb_index];
	image_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	image_barrier.subresourceRange.baseMipLevel = 0;
	image_barrier.subresourceRange.levelCount = 1;
	image_barrier.subresourceRange.baseArrayLayer = 0;
	image_barrier.subresourceRange.layerCount = 1;

	vkCmdPipelineBarrier (
		command_buffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &image_barrier);

	VkBufferImageCopy image_copy;
	memset (&image_copy, 0, sizeof (image_copy));
	image_copy.bufferOffset = 0;
	image_copy.bufferRowLength = glwidth;
	image_copy.bufferImageHeight = glheight;
	image_copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	image_copy.imageSubresource.layerCount = 1;
	image_copy.imageExtent.width = glwidth;
	image_copy.imageExtent.height = glheight;
	image_copy.imageExtent.depth = 1;

	vkCmdCopyImageToBuffer (command_buffer, swapchain_images[current_cb_index], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1, &image_copy);

	err = vkEndCommandBuffer (command_buffer);
	if (err != VK_SUCCESS)
		Sys_Error ("vkEndCommandBuffer failed");

	VkSubmitInfo submit_info;
	memset (&submit_info, 0, sizeof (submit_info));
	submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers = &command_buffer;

	err = vkQueueSubmit (vulkan_globals.queue, 1, &submit_info, VK_NULL_HANDLE);
	if (err != VK_SUCCESS)
		Sys_Error ("vkQueueSubmit failed");

	err = vkDeviceWaitIdle (vulkan_globals.device);
	if (err != VK_SUCCESS)
		Sys_Error ("vkDeviceWaitIdle failed");

	void *buffer_ptr;
	vkMapMemory (vulkan_globals.device, memory, 0, glwidth * glheight * 4, 0, &buffer_ptr);

	if (bgra)
	{
		byte	 *data = (byte *)buffer_ptr;
		const int size = glwidth * glheight * 4;
		for (i = 0; i < size; i += 4)
		{
			const byte temp = data[i];
			data[i] = data[i + 2];
			data[i + 2] = temp;
		}
	}

	if (!q_strncasecmp (ext, "png", sizeof (ext)))
		ok = Image_WritePNG (imagename, buffer_ptr, glwidth, glheight, 32, true);
	else if (!q_strncasecmp (ext, "tga", sizeof (ext)))
		ok = Image_WriteTGA (imagename, buffer_ptr, glwidth, glheight, 32, true);
	else if (!q_strncasecmp (ext, "jpg", sizeof (ext)))
		ok = Image_WriteJPG (imagename, buffer_ptr, glwidth, glheight, 32, quality, true);
	else
		ok = false;

	if (ok)
		Con_Printf ("Wrote %s\n", imagename);
	else
		Con_Printf ("SCR_ScreenShot_f: Couldn't create %s\n", imagename);

	vkUnmapMemory (vulkan_globals.device, memory);
	vkFreeMemory (vulkan_globals.device, memory, NULL);
	vkDestroyBuffer (vulkan_globals.device, buffer, NULL);
	vkFreeCommandBuffers (vulkan_globals.device, transient_command_pool, 1, &command_buffer);
}

void VID_FocusGained (void)
{
	has_focus = true;
	if (vulkan_globals.want_full_screen_exclusive)
	{
		vid.restart_next_frame = true;
	}
}

void VID_FocusLost (void)
{
	has_focus = false;
	if (vulkan_globals.want_full_screen_exclusive)
	{
		vid.restart_next_frame = true;
	}
}
