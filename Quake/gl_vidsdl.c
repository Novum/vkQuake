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
#include "cfgfile.h"
#include "bgmusic.h"
#include "resource.h"
#include "SDL.h"
#include "SDL_syswm.h"
#ifdef VK_USE_PLATFORM_XCB_KHR
#include <X11/Xlib-xcb.h> /* for XGetXCBConnection() */
#endif

#define MAX_MODE_LIST	600 //johnfitz -- was 30
#define MAX_BPPS_LIST	5
#define MAXWIDTH		10000
#define MAXHEIGHT		10000

#define NUM_COMMAND_BUFFERS 2
#define MAX_SWAP_CHAIN_IMAGES 8
#define REQUIRED_COLOR_BUFFER_FEATURES ( VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT | \
										 VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | \
										 VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT )

typedef struct {
	int			width;
	int			height;
	int			bpp;
} vmode_t;

static vmode_t	modelist[MAX_MODE_LIST];
static int		nummodes;

static qboolean	vid_initialized = false;

static SDL_Window	*draw_context;
static SDL_SysWMinfo sys_wm_info;

static qboolean	vid_locked = false; //johnfitz
static qboolean	vid_changed = false;

static void VID_Menu_Init (void); //johnfitz
static void VID_Menu_f (void); //johnfitz
static void VID_MenuDraw (void);
static void VID_MenuKey (int key);
static void VID_Restart(void);

static void ClearAllStates (void);
static void GL_InitInstance (void);
static void GL_InitDevice (void);
static void GL_CreateFrameBuffers(void);
static void GL_DestroyBeforeSetMode(void);

viddef_t	vid;				// global video state
modestate_t	modestate = MS_UNINIT;
extern qboolean scr_initialized;

//====================================

//johnfitz -- new cvars
static cvar_t	vid_fullscreen = {"vid_fullscreen", "0", CVAR_ARCHIVE};	// QuakeSpasm, was "1"
static cvar_t	vid_width = {"vid_width", "800", CVAR_ARCHIVE};		// QuakeSpasm, was 640
static cvar_t	vid_height = {"vid_height", "600", CVAR_ARCHIVE};	// QuakeSpasm, was 480
static cvar_t	vid_bpp = {"vid_bpp", "16", CVAR_ARCHIVE};
static cvar_t	vid_vsync = {"vid_vsync", "0", CVAR_ARCHIVE};
static cvar_t	vid_desktopfullscreen = {"vid_desktopfullscreen", "0", CVAR_ARCHIVE}; // QuakeSpasm
static cvar_t	vid_borderless = {"vid_borderless", "0", CVAR_ARCHIVE}; // QuakeSpasm
cvar_t	vid_filter = {"vid_filter", "0", CVAR_ARCHIVE};
cvar_t	vid_anisotropic = {"vid_anisotropic", "0", CVAR_ARCHIVE};
cvar_t vid_fsaa = {"vid_fsaa", "0", CVAR_ARCHIVE};

cvar_t		vid_gamma = {"gamma", "1", CVAR_ARCHIVE}; //johnfitz -- moved here from view.c
cvar_t		vid_contrast = {"contrast", "1", CVAR_ARCHIVE}; //QuakeSpasm, MarkV

// Vulkan
static VkInstance					vulkan_instance;
static VkPhysicalDevice				vulkan_physical_device;
static VkPhysicalDeviceFeatures		vulkan_physical_device_features;
static VkSurfaceKHR					vulkan_surface;
static VkSurfaceCapabilitiesKHR		vulkan_surface_capabilities;
static VkSwapchainKHR				vulkan_swapchain;

static uint32_t						num_swap_chain_images;
static uint32_t						current_command_buffer;
static VkCommandPool				command_pool;
static VkCommandPool				transient_command_pool;
static VkCommandBuffer				command_buffers[NUM_COMMAND_BUFFERS];
static VkFence						command_buffer_fences[NUM_COMMAND_BUFFERS];
static qboolean						command_buffer_submitted[NUM_COMMAND_BUFFERS];
static VkFramebuffer				main_framebuffers[NUM_COLOR_BUFFERS];
static VkFramebuffer				ui_framebuffers[MAX_SWAP_CHAIN_IMAGES];
static VkImage						swapchain_images[MAX_SWAP_CHAIN_IMAGES];
static VkImageView					swapchain_images_views[MAX_SWAP_CHAIN_IMAGES];
static VkSemaphore					image_aquired_semaphores[MAX_SWAP_CHAIN_IMAGES];
static VkImage						depth_buffer;
static VkDeviceMemory				depth_buffer_memory;
static VkImageView					depth_buffer_view;
static VkDeviceMemory				color_buffers_memory[NUM_COLOR_BUFFERS];
static VkImageView					color_buffers_view[NUM_COLOR_BUFFERS];
static VkImage						msaa_color_buffer;
static VkDeviceMemory				msaa_color_buffer_memory;
static VkImageView					msaa_color_buffer_view;
static VkDescriptorSet				postprocess_descriptor_set;

static PFN_vkGetDeviceProcAddr fpGetDeviceProcAddr;
static PFN_vkGetPhysicalDeviceSurfaceSupportKHR fpGetPhysicalDeviceSurfaceSupportKHR;
static PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR fpGetPhysicalDeviceSurfaceCapabilitiesKHR;
static PFN_vkGetPhysicalDeviceSurfaceFormatsKHR fpGetPhysicalDeviceSurfaceFormatsKHR;
static PFN_vkGetPhysicalDeviceSurfacePresentModesKHR fpGetPhysicalDeviceSurfacePresentModesKHR;
static PFN_vkCreateSwapchainKHR fpCreateSwapchainKHR;
static PFN_vkDestroySwapchainKHR fpDestroySwapchainKHR;
static PFN_vkGetSwapchainImagesKHR fpGetSwapchainImagesKHR;
static PFN_vkAcquireNextImageKHR fpAcquireNextImageKHR;
static PFN_vkQueuePresentKHR fpQueuePresentKHR;

#ifdef _DEBUG
static PFN_vkCreateDebugReportCallbackEXT fpCreateDebugReportCallbackEXT;
static PFN_vkDestroyDebugReportCallbackEXT fpDestroyDebugReportCallbackEXT;
PFN_vkDebugMarkerSetObjectNameEXT fpDebugMarkerSetObjectNameEXT;

VkDebugReportCallbackEXT debug_report_callback;

VkBool32 debug_message_callback(VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT obj, int64_t src, size_t loc, int32_t code, const char* pLayer,const char* pMsg, void* pUserData)
{
	const char* prefix;

	if (flags & VK_DEBUG_REPORT_ERROR_BIT_EXT)
	{
		prefix = "ERROR";
	};
	if (flags & VK_DEBUG_REPORT_WARNING_BIT_EXT)
	{
		prefix = "WARNING";
	};
	
	Sys_Printf("[Validation %s]: %s\n", prefix, pMsg);

	return VK_FALSE;
}
#endif

// Swap chain
static uint32_t current_swapchain_buffer;

#define GET_INSTANCE_PROC_ADDR(inst, entrypoint) { \
	fp##entrypoint = (PFN_vk##entrypoint)vkGetInstanceProcAddr(inst, "vk" #entrypoint); \
	if (fp##entrypoint == NULL) Sys_Error("vkGetInstanceProcAddr failed to find vk" #entrypoint); \
}

#define GET_DEVICE_PROC_ADDR(dev, entrypoint) { \
	fp##entrypoint = (PFN_vk##entrypoint)fpGetDeviceProcAddr(dev, "vk" #entrypoint); \
	if (fp##entrypoint == NULL) Sys_Error("vkGetDeviceProcAddr failed to find vk" #entrypoint); \
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
	SDL_GetWindowSize(draw_context, &w, &h);
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
	SDL_GetWindowSize(draw_context, &w, &h);
	return h;
}

/*
====================
VID_GetCurrentBPP
====================
*/
static int VID_GetCurrentBPP (void)
{
	const Uint32 pixelFormat = SDL_GetWindowPixelFormat(draw_context);
	return SDL_BITSPERPIXEL(pixelFormat);
}

/*
====================
VID_GetFullscreen
====================
*/
static qboolean VID_GetFullscreen (void)
{
	return (SDL_GetWindowFlags(draw_context) & SDL_WINDOW_FULLSCREEN) != 0;
}

/*
====================
VID_GetDesktopFullscreen
====================
*/
static qboolean VID_GetDesktopFullscreen (void)
{
	return (SDL_GetWindowFlags(draw_context) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
}

/*
====================
VID_GetVSync
====================
*/
static qboolean VID_GetVSync (void)
{
	return true;
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
	return (SDL_GetWindowFlags(draw_context) & (SDL_WINDOW_MOUSE_FOCUS | SDL_WINDOW_INPUT_FOCUS)) != 0;
}

/*
====================
VID_IsMinimized
====================
*/
qboolean VID_IsMinimized (void)
{
	return !(SDL_GetWindowFlags(draw_context) & SDL_WINDOW_SHOWN);
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
static SDL_DisplayMode *VID_SDL2_GetDisplayMode(int width, int height, int bpp)
{
	static SDL_DisplayMode mode;
	const int sdlmodes = SDL_GetNumDisplayModes(0);
	int i;

	for (i = 0; i < sdlmodes; i++)
	{
		if (SDL_GetDisplayMode(0, i, &mode) == 0
			&& mode.w == width && mode.h == height
			&& SDL_BITSPERPIXEL(mode.format) == bpp)
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
static qboolean VID_ValidMode (int width, int height, int bpp, qboolean fullscreen)
{
// ignore width / height / bpp if vid_desktopfullscreen is enabled
	if (fullscreen && vid_desktopfullscreen.value)
		return true;
	
	if (width < 320)
		return false;

	if (height < 200)
		return false;

	if (fullscreen && VID_SDL2_GetDisplayMode(width, height, bpp) == NULL)
		bpp = 0;

	switch (bpp)
	{
	case 16:
	case 24:
	case 32:
		break;
	default:
		return false;
	}

	return true;
}

/*
================
VID_SetMode
================
*/
static qboolean VID_SetMode (int width, int height, int bpp, qboolean fullscreen)
{
	int		temp;
	Uint32	flags;
	char		caption[50];
	
	// so Con_Printfs don't mess us up by forcing vid and snd updates
	temp = scr_disabled_for_loading;
	scr_disabled_for_loading = true;

	CDAudio_Pause ();
	BGM_Pause ();

	q_snprintf(caption, sizeof(caption), "vkQuake %1.2f.%d", (float)VKQUAKE_VERSION, VKQUAKE_VER_PATCH);

	/* Create the window if needed, hidden */
	if (!draw_context)
	{
		flags = SDL_WINDOW_HIDDEN;

		if (vid_borderless.value)
			flags |= SDL_WINDOW_BORDERLESS;
		
		draw_context = SDL_CreateWindow (caption, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, width, height, flags);
		if (!draw_context)
			Sys_Error ("Couldn't create window");

		SDL_VERSION(&sys_wm_info.version);
		if(!SDL_GetWindowWMInfo(draw_context,&sys_wm_info))
			Sys_Error ("Couldn't get window wm info");
	}

	/* Ensure the window is not fullscreen */
	if (VID_GetFullscreen ())
	{
		if (SDL_SetWindowFullscreen (draw_context, 0) != 0)
			Sys_Error("Couldn't set fullscreen state mode");
	}

	/* Set window size and display mode */
	SDL_SetWindowSize (draw_context, width, height);
	SDL_SetWindowPosition (draw_context, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
	SDL_SetWindowDisplayMode (draw_context, VID_SDL2_GetDisplayMode(width, height, bpp));
	SDL_SetWindowBordered (draw_context, vid_borderless.value ? SDL_FALSE : SDL_TRUE);

	/* Make window fullscreen if needed, and show the window */

	if (fullscreen) {
		Uint32 flags = vid_desktopfullscreen.value ?
			SDL_WINDOW_FULLSCREEN_DESKTOP :
			SDL_WINDOW_FULLSCREEN;
		if (SDL_SetWindowFullscreen (draw_context, flags) != 0)
			Sys_Error ("Couldn't set fullscreen state mode");
	}

	SDL_ShowWindow (draw_context);

	vid.width = VID_GetCurrentWidth();
	vid.height = VID_GetCurrentHeight();
	vid.conwidth = vid.width & 0xFFFFFFF8;
	vid.conheight = vid.conwidth * vid.height / vid.width;
	vid.numpages = 2;

	modestate = VID_GetFullscreen() ? MS_FULLSCREEN : MS_WINDOWED;

	CDAudio_Resume ();
	BGM_Resume ();
	scr_disabled_for_loading = temp;

// fix the leftover Alt from any Alt-Tab or the like that switched us away
	ClearAllStates ();

	vid.recalc_refdef = 1;

// no pending changes
	vid_changed = false;

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
static void VID_FilterChanged_f(cvar_t *var)
{
	R_InitSamplers();
}

/*
================
VID_Test -- johnfitz -- like vid_restart, but asks for confirmation after switching modes
================
*/
static void VID_Test (void)
{
	int old_width, old_height, old_bpp, old_fullscreen;

	if (vid_locked || !vid_changed)
		return;
//
// now try the switch
//
	old_width = VID_GetCurrentWidth();
	old_height = VID_GetCurrentHeight();
	old_bpp = VID_GetCurrentBPP();
	old_fullscreen = VID_GetFullscreen() ? true : false;

	VID_Restart ();

	//pop up confirmation dialoge
	if (!SCR_ModalMessage("Would you like to keep this\nvideo mode? (y/n)\n", 5.0f))
	{
		//revert cvars and mode
		Cvar_SetValueQuick (&vid_width, old_width);
		Cvar_SetValueQuick (&vid_height, old_height);
		Cvar_SetValueQuick (&vid_bpp, old_bpp);
		Cvar_SetQuick (&vid_fullscreen, old_fullscreen ? "1" : "0");
		VID_Restart ();
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
	VID_SyncCvars();
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
void GL_SetObjectName(uint64_t object, VkDebugReportObjectTypeEXT objectType, const char * name)
{
#ifdef _DEBUG
	if (fpDebugMarkerSetObjectNameEXT && name)
	{
		VkDebugMarkerObjectNameInfoEXT nameInfo;
		memset(&nameInfo, 0, sizeof(nameInfo));
		nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_MARKER_OBJECT_NAME_INFO_EXT;
		nameInfo.objectType = objectType;
		nameInfo.object = object;
		nameInfo.pObjectName = name;
		fpDebugMarkerSetObjectNameEXT(vulkan_globals.device, &nameInfo);
	};
#endif
}

/*
===============
GL_InitInstance
===============
*/
static void GL_InitInstance( void )
{
	VkResult err;
	uint32_t i;

	int found_surface_extensions = 0;

	uint32_t instance_extension_count;
	err = vkEnumerateInstanceExtensionProperties(NULL, &instance_extension_count, NULL);
	if (err == VK_SUCCESS || instance_extension_count > 0)
	{
		VkExtensionProperties *instance_extensions = (VkExtensionProperties *)
						malloc(sizeof(VkExtensionProperties) * instance_extension_count);
		err = vkEnumerateInstanceExtensionProperties(NULL, &instance_extension_count, instance_extensions);

		for (i = 0; i < instance_extension_count; ++i)
		{
			if (strcmp(VK_KHR_SURFACE_EXTENSION_NAME, instance_extensions[i].extensionName) == 0)
			{
				found_surface_extensions++;
			}

#ifdef VK_USE_PLATFORM_WIN32_KHR
#define PLATFORM_SURF_EXT VK_KHR_WIN32_SURFACE_EXTENSION_NAME
#elif VK_USE_PLATFORM_XCB_KHR
#define PLATFORM_SURF_EXT VK_KHR_XCB_SURFACE_EXTENSION_NAME
#endif

			if (strcmp(PLATFORM_SURF_EXT, instance_extensions[i].extensionName) == 0)
			{
				found_surface_extensions++;
			}
		}

		free(instance_extensions);
	}

	if(found_surface_extensions != 2)
		Sys_Error("Couldn't find %s/%s extensions", VK_KHR_SURFACE_EXTENSION_NAME, PLATFORM_SURF_EXT);
	
	VkApplicationInfo application_info;
	memset(&application_info, 0, sizeof(application_info));
	application_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	application_info.pApplicationName = "vkQuake";
	application_info.applicationVersion = 1;
	application_info.pEngineName = "vkQuake";
	application_info.engineVersion = 1;
	application_info.apiVersion = VK_API_VERSION_1_0;

	const char * const instance_extensions[] = { VK_KHR_SURFACE_EXTENSION_NAME, PLATFORM_SURF_EXT, VK_EXT_DEBUG_REPORT_EXTENSION_NAME };

	VkInstanceCreateInfo instance_create_info;
	memset(&instance_create_info, 0, sizeof(instance_create_info));
	instance_create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	instance_create_info.pApplicationInfo = &application_info;
	instance_create_info.enabledExtensionCount = 2;
	instance_create_info.ppEnabledExtensionNames = instance_extensions;
#ifdef _DEBUG
	const char * const layer_names[] = { "VK_LAYER_LUNARG_standard_validation" };

	if(vulkan_globals.validation)
	{
		Con_Printf("Using VK_LAYER_LUNARG_standard_validation\n");
		instance_create_info.enabledExtensionCount = 3;
		instance_create_info.enabledLayerCount = 1;
		instance_create_info.ppEnabledLayerNames = layer_names;
	}
#endif

	err = vkCreateInstance(&instance_create_info, NULL, &vulkan_instance);
	if (err != VK_SUCCESS)
		Sys_Error("Couldn't create Vulkan instance");

#ifdef VK_USE_PLATFORM_WIN32_KHR
	VkWin32SurfaceCreateInfoKHR surface_create_info;
	memset(&surface_create_info, 0, sizeof(surface_create_info));
	surface_create_info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
	surface_create_info.hinstance = GetModuleHandle(NULL);
	surface_create_info.hwnd = sys_wm_info.info.win.window;

	err = vkCreateWin32SurfaceKHR(vulkan_instance, &surface_create_info, NULL, &vulkan_surface);
	if (err != VK_SUCCESS)
		Sys_Error("Couldn't create Vulkan surface");
#elif VK_USE_PLATFORM_XCB_KHR
	VkXcbSurfaceCreateInfoKHR surface_create_info;
	memset(&surface_create_info, 0, sizeof(surface_create_info));
	surface_create_info.sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR;
	surface_create_info.connection = XGetXCBConnection((Display*) sys_wm_info.info.x11.display);
	surface_create_info.window = sys_wm_info.info.x11.window;

	err = vkCreateXcbSurfaceKHR(vulkan_instance, &surface_create_info, NULL, &vulkan_surface);
	if (err != VK_SUCCESS)
		Sys_Error("Couldn't create Vulkan surface");
#endif

	GET_INSTANCE_PROC_ADDR(vulkan_instance, GetDeviceProcAddr);
	GET_INSTANCE_PROC_ADDR(vulkan_instance, GetPhysicalDeviceSurfaceSupportKHR);
	GET_INSTANCE_PROC_ADDR(vulkan_instance, GetPhysicalDeviceSurfaceCapabilitiesKHR);
	GET_INSTANCE_PROC_ADDR(vulkan_instance, GetPhysicalDeviceSurfaceFormatsKHR);
	GET_INSTANCE_PROC_ADDR(vulkan_instance, GetPhysicalDeviceSurfacePresentModesKHR);
	GET_INSTANCE_PROC_ADDR(vulkan_instance, GetSwapchainImagesKHR);

#ifdef _DEBUG
	if(vulkan_globals.validation)
	{
		Con_Printf("Creating debug report callback\n");
		GET_INSTANCE_PROC_ADDR(vulkan_instance, CreateDebugReportCallbackEXT);
		GET_INSTANCE_PROC_ADDR(vulkan_instance, DestroyDebugReportCallbackEXT);

		VkDebugReportCallbackCreateInfoEXT report_callback_Info;
		memset(&report_callback_Info, 0, sizeof(report_callback_Info));
		report_callback_Info.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CREATE_INFO_EXT;
		report_callback_Info.pfnCallback = (PFN_vkDebugReportCallbackEXT)debug_message_callback;
		report_callback_Info.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT;

		err = fpCreateDebugReportCallbackEXT(vulkan_instance, &report_callback_Info, NULL, &debug_report_callback);
		if (err != VK_SUCCESS)
			Sys_Error("Could not create debug report callback");
	}
#endif
}

/*
===============
GL_InitDevice
===============
*/
static void GL_InitDevice( void )
{
	VkResult err;
	uint32_t i;

	uint32_t physical_device_count;
	err = vkEnumeratePhysicalDevices(vulkan_instance, &physical_device_count, NULL);
	if (err != VK_SUCCESS || physical_device_count == 0)
		Sys_Error("Couldn't find any Vulkan devices");

	VkPhysicalDevice *physical_devices = (VkPhysicalDevice *) malloc(sizeof(VkPhysicalDevice) * physical_device_count);
	err = vkEnumeratePhysicalDevices(vulkan_instance, &physical_device_count, physical_devices);
	vulkan_physical_device = physical_devices[0];
	free(physical_devices);

	qboolean found_swapchain_extension = false;
	qboolean found_debug_marker_extension = false;

	vkGetPhysicalDeviceMemoryProperties(vulkan_physical_device, &vulkan_globals.memory_properties);

	uint32_t device_extension_count;
	err = vkEnumerateDeviceExtensionProperties(vulkan_physical_device, NULL, &device_extension_count, NULL);

	if (err == VK_SUCCESS || device_extension_count > 0)
	{
		VkExtensionProperties *device_extensions = (VkExtensionProperties *) malloc(sizeof(VkExtensionProperties) * device_extension_count);
		err = vkEnumerateDeviceExtensionProperties(vulkan_physical_device, NULL, &device_extension_count, device_extensions);

		for (i = 0; i < device_extension_count; ++i)
		{
			if (strcmp(VK_KHR_SWAPCHAIN_EXTENSION_NAME, device_extensions[i].extensionName) == 0)
			{
				found_swapchain_extension = true;
			}
			if (strcmp(VK_EXT_DEBUG_MARKER_EXTENSION_NAME, device_extensions[i].extensionName) == 0)
			{
				found_debug_marker_extension = true;
			}
		}

		free(device_extensions);
	}

	if(!found_swapchain_extension)
		Sys_Error("Couldn't find %s extension", VK_KHR_SWAPCHAIN_EXTENSION_NAME);

	vkGetPhysicalDeviceProperties(vulkan_physical_device, &vulkan_globals.device_properties);
	switch(vulkan_globals.device_properties.vendorID)
	{
	case 0x8086:
		Con_Printf("Vendor: Intel\n");
		break;
	case 0x10DE:
		Con_Printf("Vendor: NVIDIA\n");
		break;
	case 0x1002:
		Con_Printf("Vendor: AMD\n");
		break;
	default:
		Con_Printf("Vendor: Unknown (0x%x)\n", vulkan_globals.device_properties.vendorID);
	}

	Con_Printf("Device: %s\n", vulkan_globals.device_properties.deviceName);

	qboolean found_graphics_queue = false;

	uint32_t vulkan_queue_count;
	vkGetPhysicalDeviceQueueFamilyProperties(vulkan_physical_device, &vulkan_queue_count, NULL);
	if (vulkan_queue_count == 0)
	{
		Sys_Error("Couldn't find any Vulkan queues");
	}

	VkQueueFamilyProperties * queue_family_properties = (VkQueueFamilyProperties *)malloc(vulkan_queue_count * sizeof(VkQueueFamilyProperties));
	vkGetPhysicalDeviceQueueFamilyProperties(vulkan_physical_device, &vulkan_queue_count, queue_family_properties);

	// Iterate over each queue to learn whether it supports presenting:
	VkBool32 *queue_supports_present = (VkBool32 *)malloc(vulkan_queue_count * sizeof(VkBool32));
	for (i = 0; i < vulkan_queue_count; ++i)
		fpGetPhysicalDeviceSurfaceSupportKHR(vulkan_physical_device, i, vulkan_surface, &queue_supports_present[i]);

	for (i = 0; i < vulkan_queue_count; ++i)
	{
		if (((queue_family_properties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) && queue_supports_present[i])
		{
			found_graphics_queue = true;
			vulkan_globals.gfx_queue_family_index = i;
			break;
		}
	}

	free(queue_supports_present);
	free(queue_family_properties);

	if(!found_graphics_queue)
		Sys_Error("Couldn't find graphics queue");

	float queue_priorities[] = {0.0};
	VkDeviceQueueCreateInfo queue_create_info;
	memset(&queue_create_info, 0, sizeof(queue_create_info));
	queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	queue_create_info.queueFamilyIndex = vulkan_globals.gfx_queue_family_index;
	queue_create_info.queueCount = 1;
	queue_create_info.pQueuePriorities = queue_priorities;

	const char * const device_extensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_EXT_DEBUG_MARKER_EXTENSION_NAME };

	VkDeviceCreateInfo device_create_info;
	memset(&device_create_info, 0, sizeof(device_create_info));
	device_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	device_create_info.queueCreateInfoCount = 1;
	device_create_info.pQueueCreateInfos = &queue_create_info;
	device_create_info.enabledExtensionCount = 1;
	device_create_info.ppEnabledExtensionNames = device_extensions;
#if _DEBUG
	if (found_debug_marker_extension)
		device_create_info.enabledExtensionCount = 2;
#endif

	err = vkCreateDevice(vulkan_physical_device, &device_create_info, NULL, &vulkan_globals.device);
	if (err != VK_SUCCESS)
		Sys_Error("Couldn't create Vulkan device");

	vkGetPhysicalDeviceFeatures(vulkan_physical_device, &vulkan_physical_device_features);

	GET_DEVICE_PROC_ADDR(vulkan_globals.device, CreateSwapchainKHR);
	GET_DEVICE_PROC_ADDR(vulkan_globals.device, DestroySwapchainKHR);
	GET_DEVICE_PROC_ADDR(vulkan_globals.device, GetSwapchainImagesKHR);
	GET_DEVICE_PROC_ADDR(vulkan_globals.device, AcquireNextImageKHR);
	GET_DEVICE_PROC_ADDR(vulkan_globals.device, QueuePresentKHR);

#if _DEBUG
	if (found_debug_marker_extension)
	{
		GET_DEVICE_PROC_ADDR(vulkan_globals.device, DebugMarkerSetObjectNameEXT);
	}
#endif

	vkGetDeviceQueue(vulkan_globals.device, vulkan_globals.gfx_queue_family_index, 0, &vulkan_globals.queue);

	VkFormatProperties format_properties;
	
	// Find color buffer format
	vulkan_globals.color_format = VK_FORMAT_R8G8B8A8_UNORM;
	vkGetPhysicalDeviceFormatProperties(vulkan_physical_device, VK_FORMAT_A2B10G10R10_UNORM_PACK32, &format_properties);
	qboolean a2_b10_g10_r10_support = (format_properties.optimalTilingFeatures & REQUIRED_COLOR_BUFFER_FEATURES) == REQUIRED_COLOR_BUFFER_FEATURES;
	vkGetPhysicalDeviceFormatProperties(vulkan_physical_device, VK_FORMAT_A2R10G10B10_UNORM_PACK32, &format_properties);
	qboolean a2_r10_g10_r10_support = (format_properties.optimalTilingFeatures & REQUIRED_COLOR_BUFFER_FEATURES) == REQUIRED_COLOR_BUFFER_FEATURES;

	if (a2_b10_g10_r10_support)
	{
		Con_Printf("Using A2B10G10R10 color buffer format\n");
		vulkan_globals.color_format = VK_FORMAT_A2B10G10R10_UNORM_PACK32;
	}
	else if (a2_r10_g10_r10_support)
	{
		Con_Printf("Using A2R10G10B10 color buffer format\n");
		vulkan_globals.color_format = VK_FORMAT_A2R10G10B10_UNORM_PACK32;
	}

	// Find depth format
	vkGetPhysicalDeviceFormatProperties(vulkan_physical_device, VK_FORMAT_X8_D24_UNORM_PACK32, &format_properties);
	qboolean x8_d24_support = (format_properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0;
	vkGetPhysicalDeviceFormatProperties(vulkan_physical_device, VK_FORMAT_D32_SFLOAT, &format_properties);
	qboolean d32_support = (format_properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0;

	vulkan_globals.depth_format = VK_FORMAT_D16_UNORM;
	if (x8_d24_support)
	{
		Con_Printf("Using X8_D24 depth buffer format\n");
		vulkan_globals.depth_format = VK_FORMAT_X8_D24_UNORM_PACK32;
	}
	else if(d32_support)
	{
		Con_Printf("Using D32 depth buffer format\n");
		vulkan_globals.depth_format = VK_FORMAT_D32_SFLOAT;
	}
	
}

/*
===============
GL_InitCommandBuffers
===============
*/
static void GL_InitCommandBuffers( void )
{
	int i;

	Con_Printf("Creating command buffers\n");

	VkResult err;

	VkCommandPoolCreateInfo command_pool_create_info;
	memset(&command_pool_create_info, 0, sizeof(command_pool_create_info));
	command_pool_create_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	command_pool_create_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	command_pool_create_info.queueFamilyIndex = vulkan_globals.gfx_queue_family_index;

	err = vkCreateCommandPool(vulkan_globals.device, &command_pool_create_info, NULL, &command_pool);
	if (err != VK_SUCCESS)
		Sys_Error("vkCreateCommandPool failed");

	command_pool_create_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
	command_pool_create_info.queueFamilyIndex = vulkan_globals.gfx_queue_family_index;
	err = vkCreateCommandPool(vulkan_globals.device, &command_pool_create_info, NULL, &transient_command_pool);
	if (err != VK_SUCCESS)
		Sys_Error("vkCreateCommandPool failed");

	VkCommandBufferAllocateInfo command_buffer_allocate_info;
	memset(&command_buffer_allocate_info, 0, sizeof(command_buffer_allocate_info));
	command_buffer_allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	command_buffer_allocate_info.commandPool = command_pool;
	command_buffer_allocate_info.commandBufferCount = NUM_COMMAND_BUFFERS;

	err = vkAllocateCommandBuffers(vulkan_globals.device, &command_buffer_allocate_info, command_buffers);
	if (err != VK_SUCCESS)
		Sys_Error("vkAllocateCommandBuffers failed");

	VkFenceCreateInfo fence_create_info;
	memset(&fence_create_info, 0, sizeof(fence_create_info));
	fence_create_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

	for (i = 0; i < NUM_COMMAND_BUFFERS; ++i) 
	{
		err = vkCreateFence(vulkan_globals.device, &fence_create_info, NULL, &command_buffer_fences[i]);
		if (err != VK_SUCCESS)
			Sys_Error("vkCreateFence failed");
	}
}

/*
====================
GL_CreateRenderPasses
====================
*/
static void GL_CreateRenderPasses()
{
	Con_Printf("Creating render passes\n");

	VkResult err;

	// Main render pass
	VkAttachmentDescription attachment_descriptions[4];
	memset(&attachment_descriptions, 0, sizeof(attachment_descriptions));

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

	VkSubpassDescription subpass_descriptions[2];
	memset(&subpass_descriptions, 0, sizeof(subpass_descriptions));

	subpass_descriptions[0].colorAttachmentCount = 1;
	subpass_descriptions[0].pColorAttachments = &scene_color_attachment_reference;
	subpass_descriptions[0].pDepthStencilAttachment = &depth_attachment_reference;
	subpass_descriptions[0].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	if (resolve)
		subpass_descriptions[0].pResolveAttachments = &resolve_attachment_reference;

	VkRenderPassCreateInfo render_pass_create_info;
	memset(&render_pass_create_info, 0, sizeof(render_pass_create_info));
	render_pass_create_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	render_pass_create_info.attachmentCount = resolve ? 3 : 2;
	render_pass_create_info.pAttachments = attachment_descriptions;
	render_pass_create_info.subpassCount = 1;
	render_pass_create_info.pSubpasses = subpass_descriptions;

	err = vkCreateRenderPass(vulkan_globals.device, &render_pass_create_info, NULL, &vulkan_globals.main_render_pass);
	if (err != VK_SUCCESS)
		Sys_Error("Couldn't create Vulkan render pass");

	GL_SetObjectName((uint64_t)vulkan_globals.main_render_pass, VK_DEBUG_REPORT_OBJECT_TYPE_RENDER_PASS_EXT, "main");

	// UI Render Pass
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

	memset(&subpass_descriptions, 0, sizeof(subpass_descriptions));
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
	subpass_dependencies[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	subpass_dependencies[0].dependencyFlags = 0;

	memset(&render_pass_create_info, 0, sizeof(render_pass_create_info));
	render_pass_create_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	render_pass_create_info.attachmentCount = 2;
	render_pass_create_info.pAttachments = attachment_descriptions;
	render_pass_create_info.subpassCount = 2;
	render_pass_create_info.pSubpasses = subpass_descriptions;
	render_pass_create_info.dependencyCount = 1;
	render_pass_create_info.pDependencies = subpass_dependencies;

	err = vkCreateRenderPass(vulkan_globals.device, &render_pass_create_info, NULL, &vulkan_globals.ui_render_pass);
	if (err != VK_SUCCESS)
		Sys_Error("Couldn't create Vulkan render pass");

	GL_SetObjectName((uint64_t)vulkan_globals.main_render_pass, VK_DEBUG_REPORT_OBJECT_TYPE_RENDER_PASS_EXT, "ui");

	if(vulkan_globals.warp_render_pass == VK_NULL_HANDLE)
	{
		// Warp rendering
		attachment_descriptions[0].format = VK_FORMAT_R8G8B8A8_UNORM;
		attachment_descriptions[0].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachment_descriptions[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachment_descriptions[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachment_descriptions[0].finalLayout = VK_IMAGE_LAYOUT_GENERAL;
		scene_color_attachment_reference.attachment = 0;
		scene_color_attachment_reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		VkSubpassDescription subpass_description;
		memset(&subpass_description, 0, sizeof(subpass_description));
		subpass_description.colorAttachmentCount = 1;
		subpass_description.pColorAttachments = &scene_color_attachment_reference;
		subpass_description.pDepthStencilAttachment = NULL;
		subpass_description.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;

		render_pass_create_info.subpassCount = 1;
		render_pass_create_info.pSubpasses = &subpass_description;
		render_pass_create_info.attachmentCount = 1;
		render_pass_create_info.dependencyCount = 0;
		render_pass_create_info.pDependencies = NULL;

		err = vkCreateRenderPass(vulkan_globals.device, &render_pass_create_info, NULL, &vulkan_globals.warp_render_pass);
		if (err != VK_SUCCESS)
			Sys_Error("Couldn't create Vulkan render pass");

		GL_SetObjectName((uint64_t)vulkan_globals.warp_render_pass, VK_DEBUG_REPORT_OBJECT_TYPE_RENDER_PASS_EXT, "warp");
	}
}

/*
===============
GL_CreateDepthBuffer
===============
*/
static void GL_CreateDepthBuffer( void )
{
	Con_Printf("Creating depth buffer\n");

	VkResult err;
	
	VkImageCreateInfo image_create_info;
	memset(&image_create_info, 0, sizeof(image_create_info));
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

	err = vkCreateImage(vulkan_globals.device, &image_create_info, NULL, &depth_buffer);
	if (err != VK_SUCCESS)
		Sys_Error("vkCreateImage failed");

	GL_SetObjectName((uint64_t)depth_buffer, VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_EXT, "Depth Buffer");

	VkMemoryRequirements memory_requirements;
	vkGetImageMemoryRequirements(vulkan_globals.device, depth_buffer, &memory_requirements);

	VkMemoryAllocateInfo memory_allocate_info;
	memset(&memory_allocate_info, 0, sizeof(memory_allocate_info));
	memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	memory_allocate_info.allocationSize = memory_requirements.size;
	memory_allocate_info.memoryTypeIndex = GL_MemoryTypeFromProperties(memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);

	num_vulkan_misc_allocations += 1;
	err = vkAllocateMemory(vulkan_globals.device, &memory_allocate_info, NULL, &depth_buffer_memory);
	if (err != VK_SUCCESS)
		Sys_Error("vkAllocateMemory failed");

	GL_SetObjectName((uint64_t)depth_buffer_memory, VK_DEBUG_REPORT_OBJECT_TYPE_DEVICE_MEMORY_EXT, "Depth Buffer");

	err = vkBindImageMemory(vulkan_globals.device, depth_buffer, depth_buffer_memory, 0);
	if (err != VK_SUCCESS)
		Sys_Error("vkBindImageMemory failed");

	VkImageViewCreateInfo image_view_create_info;
	memset(&image_view_create_info, 0, sizeof(image_view_create_info));
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

	err = vkCreateImageView(vulkan_globals.device, &image_view_create_info, NULL, &depth_buffer_view);
	if (err != VK_SUCCESS)
		Sys_Error("vkCreateImageView failed");

	GL_SetObjectName((uint64_t)depth_buffer_view, VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_VIEW_EXT, "Depth Buffer View");
}


/*
===============
GL_CreateColorBuffer
===============
*/
static void GL_CreateColorBuffer( void )
{
	VkResult err;
	int i;

	Con_Printf("Creating color buffer\n");

	VkImageCreateInfo image_create_info;
	memset(&image_create_info, 0, sizeof(image_create_info));
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
	image_create_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;

	for (i = 0; i < NUM_COLOR_BUFFERS; ++i)
	{
		err = vkCreateImage(vulkan_globals.device, &image_create_info, NULL, &vulkan_globals.color_buffers[i]);
		if (err != VK_SUCCESS)
			Sys_Error("vkCreateImage failed");
	
		GL_SetObjectName((uint64_t)vulkan_globals.color_buffers[i], VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_EXT, va("Color Buffer %d", i));

		VkMemoryRequirements memory_requirements;
		vkGetImageMemoryRequirements(vulkan_globals.device, vulkan_globals.color_buffers[i], &memory_requirements);

		VkMemoryAllocateInfo memory_allocate_info;
		memset(&memory_allocate_info, 0, sizeof(memory_allocate_info));
		memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		memory_allocate_info.allocationSize = memory_requirements.size;
		memory_allocate_info.memoryTypeIndex = GL_MemoryTypeFromProperties(memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);

		num_vulkan_misc_allocations += 1;
		err = vkAllocateMemory(vulkan_globals.device, &memory_allocate_info, NULL, &color_buffers_memory[i]);
		if (err != VK_SUCCESS)
			Sys_Error("vkAllocateMemory failed");

		GL_SetObjectName((uint64_t)color_buffers_memory[i], VK_DEBUG_REPORT_OBJECT_TYPE_DEVICE_MEMORY_EXT, va("Color Buffer %d", i));

		err = vkBindImageMemory(vulkan_globals.device, vulkan_globals.color_buffers[i], color_buffers_memory[i], 0);
		if (err != VK_SUCCESS)
			Sys_Error("vkBindImageMemory failed");

		VkImageViewCreateInfo image_view_create_info;
		memset(&image_view_create_info, 0, sizeof(image_view_create_info));
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

		err = vkCreateImageView(vulkan_globals.device, &image_view_create_info, NULL, &color_buffers_view[i]);
		if (err != VK_SUCCESS)
			Sys_Error("vkCreateImageView failed");

		GL_SetObjectName((uint64_t)color_buffers_view[i], VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_VIEW_EXT, va("Color Buffer View %d", i));
	}

	vulkan_globals.sample_count = VK_SAMPLE_COUNT_1_BIT;
	vulkan_globals.supersampling = false;

	if (vid_fsaa.value)
	{
		VkImageFormatProperties image_format_properties;
		vkGetPhysicalDeviceImageFormatProperties(vulkan_physical_device, vulkan_globals.color_format, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, 0, &image_format_properties);

		if (image_format_properties.sampleCounts & VK_SAMPLE_COUNT_2_BIT)
			vulkan_globals.sample_count = VK_SAMPLE_COUNT_2_BIT;
		if (image_format_properties.sampleCounts & VK_SAMPLE_COUNT_4_BIT)
			vulkan_globals.sample_count = VK_SAMPLE_COUNT_4_BIT;
		if (image_format_properties.sampleCounts & VK_SAMPLE_COUNT_8_BIT)
			vulkan_globals.sample_count = VK_SAMPLE_COUNT_8_BIT;
		if (image_format_properties.sampleCounts & VK_SAMPLE_COUNT_16_BIT)
			vulkan_globals.sample_count = VK_SAMPLE_COUNT_16_BIT;

		switch(vulkan_globals.sample_count)
		{
			case VK_SAMPLE_COUNT_2_BIT:
				Con_Printf("2 AA Samples\n");
				break;
			case VK_SAMPLE_COUNT_4_BIT:
				Con_Printf("4 AA Samples\n");
				break;
			case VK_SAMPLE_COUNT_8_BIT:
				Con_Printf("8 AA Samples\n");
				break;
			case VK_SAMPLE_COUNT_16_BIT:
				Con_Printf("16 AA Samples\n");
				break;
			default:
				break;
		}

		vulkan_globals.supersampling = (vulkan_physical_device_features.sampleRateShading && vid_fsaa.value >= 2) ? true : false;

		if (vulkan_globals.supersampling)
			Con_Printf( "Supersampling enabled\n" );
	}

	if (vulkan_globals.sample_count != VK_SAMPLE_COUNT_1_BIT)
	{
		image_create_info.samples = vulkan_globals.sample_count;

		err = vkCreateImage(vulkan_globals.device, &image_create_info, NULL, &msaa_color_buffer);
		if (err != VK_SUCCESS)
			Sys_Error("vkCreateImage failed");

		GL_SetObjectName((uint64_t)msaa_color_buffer, VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_EXT, "MSAA Color Buffer");
	
		VkMemoryRequirements memory_requirements;
		vkGetImageMemoryRequirements(vulkan_globals.device, msaa_color_buffer, &memory_requirements);

		VkMemoryAllocateInfo memory_allocate_info;
		memset(&memory_allocate_info, 0, sizeof(memory_allocate_info));
		memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		memory_allocate_info.allocationSize = memory_requirements.size;
		memory_allocate_info.memoryTypeIndex = GL_MemoryTypeFromProperties(memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);

		num_vulkan_misc_allocations += 1;
		err = vkAllocateMemory(vulkan_globals.device, &memory_allocate_info, NULL, &msaa_color_buffer_memory);
		if (err != VK_SUCCESS)
			Sys_Error("vkAllocateMemory failed");

		GL_SetObjectName((uint64_t)msaa_color_buffer_memory, VK_DEBUG_REPORT_OBJECT_TYPE_DEVICE_MEMORY_EXT, "MSAA Color Buffer");

		err = vkBindImageMemory(vulkan_globals.device, msaa_color_buffer, msaa_color_buffer_memory, 0);
		if (err != VK_SUCCESS)
			Sys_Error("vkBindImageMemory failed");

		VkImageViewCreateInfo image_view_create_info;
		memset(&image_view_create_info, 0, sizeof(image_view_create_info));
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

		err = vkCreateImageView(vulkan_globals.device, &image_view_create_info, NULL, &msaa_color_buffer_view);
		if (err != VK_SUCCESS)
			Sys_Error("vkCreateImageView failed");
	}
}

/*
===============
GL_CreateDescriptorSets
===============
*/
static void GL_CreateDescriptorSets(void)
{
	VkDescriptorSetAllocateInfo descriptor_set_allocate_info;
	memset(&descriptor_set_allocate_info, 0, sizeof(descriptor_set_allocate_info));
	descriptor_set_allocate_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	descriptor_set_allocate_info.descriptorPool = vulkan_globals.descriptor_pool;
	descriptor_set_allocate_info.descriptorSetCount = 1;
	descriptor_set_allocate_info.pSetLayouts = &vulkan_globals.input_attachment_set_layout;

	vkAllocateDescriptorSets(vulkan_globals.device, &descriptor_set_allocate_info, &postprocess_descriptor_set);

	VkDescriptorImageInfo image_info;
	memset(&image_info, 0, sizeof(image_info));
	image_info.imageView = color_buffers_view[0];
	image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	VkWriteDescriptorSet input_attachment_write;
	memset(&input_attachment_write, 0, sizeof(input_attachment_write));
	input_attachment_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	input_attachment_write.dstBinding = 0;
	input_attachment_write.dstArrayElement = 0;
	input_attachment_write.descriptorCount = 1;
	input_attachment_write.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
	input_attachment_write.dstSet = postprocess_descriptor_set;
	input_attachment_write.pImageInfo = &image_info;
	vkUpdateDescriptorSets(vulkan_globals.device, 1, &input_attachment_write, 0, NULL);

	memset(&descriptor_set_allocate_info, 0, sizeof(descriptor_set_allocate_info));
	descriptor_set_allocate_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	descriptor_set_allocate_info.descriptorPool = vulkan_globals.descriptor_pool;
	descriptor_set_allocate_info.descriptorSetCount = 1;
	descriptor_set_allocate_info.pSetLayouts = &vulkan_globals.screen_warp_set_layout;

	vkAllocateDescriptorSets(vulkan_globals.device, &descriptor_set_allocate_info, &vulkan_globals.screen_warp_desc_set);

	VkDescriptorImageInfo input_image_info;
	memset(&input_image_info, 0, sizeof(input_image_info));
	input_image_info.imageView = color_buffers_view[1];
	input_image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	input_image_info.sampler = vulkan_globals.linear_sampler;

	VkDescriptorImageInfo output_image_info;
	memset(&output_image_info, 0, sizeof(output_image_info));
	output_image_info.imageView = color_buffers_view[0];
	output_image_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

	VkWriteDescriptorSet screen_warp_writes[2];
	memset(screen_warp_writes, 0, sizeof(screen_warp_writes));
	screen_warp_writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	screen_warp_writes[0].dstBinding = 0;
	screen_warp_writes[0].dstArrayElement = 0;
	screen_warp_writes[0].descriptorCount = 1;
	screen_warp_writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	screen_warp_writes[0].dstSet = vulkan_globals.screen_warp_desc_set;
	screen_warp_writes[0].pImageInfo = &input_image_info;
	screen_warp_writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	screen_warp_writes[1].dstBinding = 1;
	screen_warp_writes[1].dstArrayElement = 0;
	screen_warp_writes[1].descriptorCount = 1;
	screen_warp_writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	screen_warp_writes[1].dstSet = vulkan_globals.screen_warp_desc_set;
	screen_warp_writes[1].pImageInfo = &output_image_info;

	vkUpdateDescriptorSets(vulkan_globals.device, 2, screen_warp_writes, 0, NULL);
}

/*
===============
GL_CreateSwapChain
===============
*/
static void GL_CreateSwapChain( void )
{
	uint32_t i;

	Con_Printf("Creating swap chain\n");

	VkResult err;

	err = fpGetPhysicalDeviceSurfaceCapabilitiesKHR(vulkan_physical_device, vulkan_surface, &vulkan_surface_capabilities);
	if (err != VK_SUCCESS)
		Sys_Error("Couldn't get surface capabilities");
	if (vulkan_surface_capabilities.currentExtent.width != vid.width || vulkan_surface_capabilities.currentExtent.height != vid.height)
		Sys_Error("Surface doesn't match video width or height");

	uint32_t format_count;
	err = fpGetPhysicalDeviceSurfaceFormatsKHR(vulkan_physical_device, vulkan_surface, &format_count, NULL);
	if (err != VK_SUCCESS)
		Sys_Error("Couldn't get surface formats");

	VkSurfaceFormatKHR *surface_formats = (VkSurfaceFormatKHR *)malloc(format_count * sizeof(VkSurfaceFormatKHR));
	err = fpGetPhysicalDeviceSurfaceFormatsKHR(vulkan_physical_device, vulkan_surface, &format_count, surface_formats);
	if (err != VK_SUCCESS)
		Sys_Error("fpGetPhysicalDeviceSurfaceFormatsKHR failed");

	uint32_t present_mode_count = 0;
	err = fpGetPhysicalDeviceSurfacePresentModesKHR(vulkan_physical_device, vulkan_surface, &present_mode_count, NULL);
	if (err != VK_SUCCESS)
		Sys_Error("fpGetPhysicalDeviceSurfacePresentModesKHR failed");

	VkPresentModeKHR * present_modes = (VkPresentModeKHR *) malloc(present_mode_count * sizeof(VkPresentModeKHR));
	err = fpGetPhysicalDeviceSurfacePresentModesKHR(vulkan_physical_device, vulkan_surface, &present_mode_count, present_modes);
	if (err != VK_SUCCESS)
		Sys_Error("fpGetPhysicalDeviceSurfacePresentModesKHR failed");

	// VK_PRESENT_MODE_FIFO_KHR is always supported
	VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;
	if (vid_vsync.value == 0)
	{
		qboolean found_immediate = false;
		qboolean found_mailbox = false;
		for (i = 0; i < present_mode_count; ++i)
		{
			if(present_modes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR)
				found_immediate = true;
			if(present_modes[i] == VK_PRESENT_MODE_MAILBOX_KHR)
				found_mailbox = true;
		}

		if (found_mailbox)
			present_mode = VK_PRESENT_MODE_MAILBOX_KHR;
		if (found_immediate)
			present_mode = VK_PRESENT_MODE_IMMEDIATE_KHR;
	}

	free(present_modes);

	switch(present_mode) {
	case VK_PRESENT_MODE_FIFO_KHR:
		Con_Printf("Using FIFO present mode\n");
		break;
	case VK_PRESENT_MODE_MAILBOX_KHR:
		Con_Printf("Using MAILBOX present mode\n");
		break;
	case VK_PRESENT_MODE_IMMEDIATE_KHR:
		Con_Printf("Using IMMEDIATE present mode\n");
		break;
	default:
		break;
	}

	VkSwapchainCreateInfoKHR swapchain_create_info;
	memset(&swapchain_create_info, 0, sizeof(swapchain_create_info));
	swapchain_create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	swapchain_create_info.pNext = NULL;
	swapchain_create_info.surface = vulkan_surface;
	swapchain_create_info.minImageCount = 2;
	swapchain_create_info.imageFormat = surface_formats[0].format;
	swapchain_create_info.imageColorSpace = surface_formats[0].colorSpace;
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

	vulkan_globals.swap_chain_format = surface_formats[0].format;
	free(surface_formats);

	err = fpCreateSwapchainKHR(vulkan_globals.device, &swapchain_create_info, NULL, &vulkan_swapchain);
	if (err != VK_SUCCESS)
		Sys_Error("Couldn't create swap chain");

	err = fpGetSwapchainImagesKHR(vulkan_globals.device, vulkan_swapchain, &num_swap_chain_images, NULL);
	if (err != VK_SUCCESS || num_swap_chain_images > MAX_SWAP_CHAIN_IMAGES)
		Sys_Error("Couldn't get swap chain images");

	fpGetSwapchainImagesKHR(vulkan_globals.device, vulkan_swapchain, &num_swap_chain_images, swapchain_images);

	VkImageViewCreateInfo image_view_create_info;
	memset(&image_view_create_info, 0, sizeof(image_view_create_info));
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
	memset(&semaphore_create_info, 0, sizeof(semaphore_create_info));
	semaphore_create_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

	for (i = 0; i < num_swap_chain_images; ++i)
	{
		GL_SetObjectName((uint64_t)swapchain_images[i], VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_EXT, "Swap Chain");

		image_view_create_info.image = swapchain_images[i];
		err = vkCreateImageView(vulkan_globals.device, &image_view_create_info, NULL, &swapchain_images_views[i]);
		if (err != VK_SUCCESS)
			Sys_Error("vkCreateImageView failed");

		GL_SetObjectName((uint64_t)swapchain_images_views[i], VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_VIEW_EXT, "Swap Chain View");

		err = vkCreateSemaphore(vulkan_globals.device, &semaphore_create_info, NULL, &image_aquired_semaphores[i]);
		if (err != VK_SUCCESS)
			Sys_Error("vkCreateSemaphore failed");
	}
}


/*
===============
GL_CreateFrameBuffers
===============
*/
static void GL_CreateFrameBuffers( void )
{
	uint32_t i;

	Con_Printf("Creating frame buffers\n");

	VkResult err;

	const qboolean resolve = ( vulkan_globals.sample_count != VK_SAMPLE_COUNT_1_BIT);

	for (i = 0; i < NUM_COLOR_BUFFERS; ++i)
	{
		VkFramebufferCreateInfo framebuffer_create_info;
		memset(&framebuffer_create_info, 0, sizeof(framebuffer_create_info));
		framebuffer_create_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		framebuffer_create_info.renderPass = vulkan_globals.main_render_pass;
		framebuffer_create_info.attachmentCount = resolve ? 3 : 2;
		framebuffer_create_info.width = vid.width;
		framebuffer_create_info.height = vid.height;
		framebuffer_create_info.layers = 1;

		VkImageView attachments[3] = { color_buffers_view[i], depth_buffer_view, msaa_color_buffer_view };
		framebuffer_create_info.pAttachments = attachments;

		err = vkCreateFramebuffer(vulkan_globals.device, &framebuffer_create_info, NULL, &main_framebuffers[i]);
		if (err != VK_SUCCESS)
			Sys_Error("vkCreateFramebuffer failed");

		GL_SetObjectName((uint64_t)main_framebuffers[i], VK_DEBUG_REPORT_OBJECT_TYPE_FRAMEBUFFER_EXT, "main");
	}

	for (i = 0; i < num_swap_chain_images; ++i)
	{
		VkFramebufferCreateInfo framebuffer_create_info;
		memset(&framebuffer_create_info, 0, sizeof(framebuffer_create_info));
		framebuffer_create_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		framebuffer_create_info.renderPass = vulkan_globals.ui_render_pass;
		framebuffer_create_info.attachmentCount = 2;
		framebuffer_create_info.width = vid.width;
		framebuffer_create_info.height = vid.height;
		framebuffer_create_info.layers = 1;

		VkImageView attachments[2] = { color_buffers_view[0],  swapchain_images_views[i] };
		framebuffer_create_info.pAttachments = attachments;

		err = vkCreateFramebuffer(vulkan_globals.device, &framebuffer_create_info, NULL, &ui_framebuffers[i]);
		if (err != VK_SUCCESS)
			Sys_Error("vkCreateFramebuffer failed");

		GL_SetObjectName((uint64_t)ui_framebuffers[i], VK_DEBUG_REPORT_OBJECT_TYPE_FRAMEBUFFER_EXT, "ui");
	}
}

/*
===============
GL_DestroyBeforeSetMode
===============
*/
static void GL_DestroyBeforeSetMode( void )
{
	uint32_t i;

	GL_WaitForDeviceIdle();

	vkFreeDescriptorSets(vulkan_globals.device, vulkan_globals.descriptor_pool, 1, &postprocess_descriptor_set);
	postprocess_descriptor_set = VK_NULL_HANDLE;

	vkFreeDescriptorSets(vulkan_globals.device, vulkan_globals.descriptor_pool, 1, &vulkan_globals.screen_warp_desc_set);
	vulkan_globals.screen_warp_desc_set = VK_NULL_HANDLE;

	if (msaa_color_buffer)
	{
		vkDestroyImageView(vulkan_globals.device, msaa_color_buffer_view, NULL);
		vkDestroyImage(vulkan_globals.device, msaa_color_buffer, NULL);
		num_vulkan_misc_allocations -= 1;
		vkFreeMemory(vulkan_globals.device, msaa_color_buffer_memory, NULL);

		msaa_color_buffer_view = VK_NULL_HANDLE;
		msaa_color_buffer = VK_NULL_HANDLE;
		msaa_color_buffer_memory = VK_NULL_HANDLE;
	}

	for (i = 0; i < NUM_COLOR_BUFFERS; ++i)
	{
		vkDestroyImageView(vulkan_globals.device, color_buffers_view[i], NULL);
		vkDestroyImage(vulkan_globals.device, vulkan_globals.color_buffers[i], NULL);
		num_vulkan_misc_allocations -= 1;
		vkFreeMemory(vulkan_globals.device, color_buffers_memory[i], NULL);

		color_buffers_view[i] = VK_NULL_HANDLE;
		vulkan_globals.color_buffers[i] = VK_NULL_HANDLE;
		color_buffers_memory[i] = VK_NULL_HANDLE;
	}

	vkDestroyImageView(vulkan_globals.device, depth_buffer_view, NULL);
	vkDestroyImage(vulkan_globals.device, depth_buffer, NULL);
	num_vulkan_misc_allocations -= 1;
	vkFreeMemory(vulkan_globals.device, depth_buffer_memory, NULL);

	depth_buffer_view = VK_NULL_HANDLE;
	depth_buffer = VK_NULL_HANDLE;
	depth_buffer_memory = VK_NULL_HANDLE;

	for (i = 0; i < num_swap_chain_images; ++i)
	{
		vkDestroyImageView(vulkan_globals.device, swapchain_images_views[i], NULL);
		swapchain_images_views[i] = VK_NULL_HANDLE;
		vkDestroyFramebuffer(vulkan_globals.device, ui_framebuffers[i], NULL);
		ui_framebuffers[i] = VK_NULL_HANDLE;
	}

	for (i = 0; i < NUM_COLOR_BUFFERS; ++i)
	{
		vkDestroyFramebuffer(vulkan_globals.device, main_framebuffers[i], NULL);
		main_framebuffers[i] = VK_NULL_HANDLE;
	}

	fpDestroySwapchainKHR(vulkan_globals.device, vulkan_swapchain, NULL);

	vkDestroyRenderPass(vulkan_globals.device, vulkan_globals.main_render_pass, NULL);
}

/*
=================
GL_BeginRendering
=================
*/
void GL_BeginRendering (int *x, int *y, int *width, int *height)
{
	int i;

	R_SwapDynamicBuffers();

	vulkan_globals.device_idle = false;
	*x = *y = 0;
	*width = vid.width;
	*height = vid.height;

	VkResult err;

	if (command_buffer_submitted[current_command_buffer])
	{
		err = vkWaitForFences(vulkan_globals.device, 1, &command_buffer_fences[current_command_buffer], VK_TRUE, UINT64_MAX);
		if (err != VK_SUCCESS)
			Sys_Error("vkWaitForFences failed");
	}

	err = vkResetFences(vulkan_globals.device, 1, &command_buffer_fences[current_command_buffer]);
	if (err != VK_SUCCESS)
		Sys_Error("vkResetFences failed");

	VkCommandBufferBeginInfo command_buffer_begin_info;
	memset(&command_buffer_begin_info, 0, sizeof(command_buffer_begin_info));
	command_buffer_begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

	vulkan_globals.command_buffer = command_buffers[current_command_buffer];
	err = vkBeginCommandBuffer(vulkan_globals.command_buffer, &command_buffer_begin_info);
	if (err != VK_SUCCESS)
		Sys_Error("vkBeginCommandBuffer failed");

	err = fpAcquireNextImageKHR(vulkan_globals.device, vulkan_swapchain, UINT64_MAX, image_aquired_semaphores[current_command_buffer], VK_NULL_HANDLE, &current_swapchain_buffer);
	if (err != VK_SUCCESS)
		Sys_Error("Couldn't acquire next image");

	VkRect2D render_area;
	render_area.offset.x = 0;
	render_area.offset.y = 0;
	render_area.extent.width = vid.width;
	render_area.extent.height = vid.height;

	VkClearValue depth_clear_value;
	depth_clear_value.depthStencil.depth = 1.0f;
	depth_clear_value.depthStencil.stencil = 0;

	vulkan_globals.main_clear_values[0] = vulkan_globals.color_clear_value;
	vulkan_globals.main_clear_values[1] = depth_clear_value;
	vulkan_globals.main_clear_values[2] = vulkan_globals.color_clear_value;

	const qboolean resolve = (vulkan_globals.sample_count != VK_SAMPLE_COUNT_1_BIT);
	
	memset(&vulkan_globals.main_render_pass_begin_infos, 0, sizeof(vulkan_globals.main_render_pass_begin_infos));
	for (i = 0; i < 2; ++i)
	{
		vulkan_globals.main_render_pass_begin_infos[i].sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		vulkan_globals.main_render_pass_begin_infos[i].renderArea = render_area;
		vulkan_globals.main_render_pass_begin_infos[i].renderPass = vulkan_globals.main_render_pass;
		vulkan_globals.main_render_pass_begin_infos[i].framebuffer = main_framebuffers[i];
		vulkan_globals.main_render_pass_begin_infos[i].clearValueCount = resolve ? 3 : 2;
		vulkan_globals.main_render_pass_begin_infos[i].pClearValues = vulkan_globals.main_clear_values;
	}

	memset(&vulkan_globals.ui_render_pass_begin_info, 0, sizeof(vulkan_globals.ui_render_pass_begin_info));
	vulkan_globals.ui_render_pass_begin_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	vulkan_globals.ui_render_pass_begin_info.renderArea = render_area;
	vulkan_globals.ui_render_pass_begin_info.renderPass = vulkan_globals.ui_render_pass;
	vulkan_globals.ui_render_pass_begin_info.framebuffer = ui_framebuffers[current_swapchain_buffer];
	vulkan_globals.ui_render_pass_begin_info.clearValueCount = 0;

	vkCmdSetScissor(vulkan_globals.command_buffer, 0, 1, &render_area);

	VkViewport viewport;
	viewport.x = 0;
	viewport.y = 0;
	viewport.width = vid.width;
	viewport.height = vid.height;
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;

	vkCmdSetViewport(vulkan_globals.command_buffer, 0, 1, &viewport);
}

/*
=================
GL_EndRendering
=================
*/
void GL_EndRendering (void)
{
	R_SubmitStagingBuffers();
	R_FlushDynamicBuffers();
	
	VkResult err;

	// Render post process
	GL_Viewport(0, 0, vid.width, vid.height);
	float postprocess_values[2] = { vid_gamma.value, q_min(2.0f, q_max(1.0f, vid_contrast.value)) };

	vkCmdNextSubpass(vulkan_globals.command_buffer, VK_SUBPASS_CONTENTS_INLINE);
	vkCmdBindDescriptorSets(vulkan_globals.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.postprocess_pipeline_layout, 0, 1, &postprocess_descriptor_set, 0, NULL);
	vkCmdBindPipeline(vulkan_globals.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.postprocess_pipeline);
	vkCmdPushConstants(vulkan_globals.command_buffer, vulkan_globals.postprocess_pipeline_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, 2 * sizeof(float), postprocess_values);
	vkCmdDraw(vulkan_globals.command_buffer, 3, 1, 0, 0);

	vkCmdEndRenderPass(vulkan_globals.command_buffer);

	err = vkEndCommandBuffer(vulkan_globals.command_buffer);
	if (err != VK_SUCCESS)
		Sys_Error("vkEndCommandBuffer failed");

	VkPipelineStageFlags wait_dst_stage_mask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;

	VkSubmitInfo submit_info;
	memset(&submit_info, 0, sizeof(submit_info));
	submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers = &command_buffers[current_command_buffer];
	submit_info.waitSemaphoreCount = 1;
	submit_info.pWaitSemaphores = &image_aquired_semaphores[current_command_buffer];
	submit_info.pWaitDstStageMask = &wait_dst_stage_mask;

	err = vkQueueSubmit(vulkan_globals.queue, 1, &submit_info, command_buffer_fences[current_command_buffer]);
	if (err != VK_SUCCESS)
		Sys_Error("vkQueueSubmit failed");

	vulkan_globals.device_idle = false;

	command_buffer_submitted[current_command_buffer] = true;
	current_command_buffer = (current_command_buffer + 1) % NUM_COMMAND_BUFFERS;

	VkPresentInfoKHR present_info;
	memset(&present_info, 0, sizeof(present_info));
	present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	present_info.swapchainCount = 1;
	present_info.pSwapchains = &vulkan_swapchain,
	present_info.pImageIndices = &current_swapchain_buffer;
	err = fpQueuePresentKHR(vulkan_globals.queue, &present_info);
	if (err != VK_SUCCESS)
		Sys_Error("vkQueuePresentKHR failed");
}

/*
=================
GL_WaitForDeviceIdle
=================
*/
void GL_WaitForDeviceIdle()
{
	if (!vulkan_globals.device_idle)
	{
		R_SubmitStagingBuffers();
		vkDeviceWaitIdle(vulkan_globals.device);
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
		SDL_QuitSubSystem(SDL_INIT_VIDEO);
		draw_context = NULL;
		PL_VID_Shutdown();
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
		Con_Printf("%dx%dx%d %s\n",
			VID_GetCurrentWidth(),
			VID_GetCurrentHeight(),
			VID_GetCurrentBPP(),
			VID_GetFullscreen() ? "fullscreen" : "windowed");
}

/*
=================
VID_DescribeModes_f -- johnfitz -- changed formatting, and added refresh rates after each mode.
=================
*/
static void VID_DescribeModes_f (void)
{
	int	i;
	int	lastwidth, lastheight, lastbpp, count;

	lastwidth = lastheight = lastbpp = count = 0;

	for (i = 0; i < nummodes; i++)
	{
		if (lastwidth != modelist[i].width || lastheight != modelist[i].height || lastbpp != modelist[i].bpp)
		{
			if (count > 0)
				Con_SafePrintf ("\n");
			Con_SafePrintf ("   %4i x %4i x %i", modelist[i].width, modelist[i].height, modelist[i].bpp);
			lastwidth = modelist[i].width;
			lastheight = modelist[i].height;
			lastbpp = modelist[i].bpp;
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
	const int sdlmodes = SDL_GetNumDisplayModes(0);
	int i;

	nummodes = 0;
	for (i = 0; i < sdlmodes; i++)
	{
		SDL_DisplayMode mode;

		if (nummodes >= MAX_MODE_LIST)
			break;
		if (SDL_GetDisplayMode(0, i, &mode) == 0)
		{
			modelist[nummodes].width = mode.w;
			modelist[nummodes].height = mode.h;
			modelist[nummodes].bpp = SDL_BITSPERPIXEL(mode.format);
			nummodes++;
		}
	}
}

/*
===================
VID_Init
===================
*/
void	VID_Init (void)
{
	static char vid_center[] = "SDL_VIDEO_CENTERED=center";
	int		p, width, height, bpp, display_width, display_height, display_bpp;
	qboolean	fullscreen;
	const char	*read_vars[] = { "vid_fullscreen",
					 "vid_width",
					 "vid_height",
					 "vid_bpp",
					 "vid_vsync",
					 "vid_desktopfullscreen",
					 "vid_fsaa",
					 "vid_borderless"};
#define num_readvars	( sizeof(read_vars)/sizeof(read_vars[0]) )

	Cvar_RegisterVariable (&vid_fullscreen); //johnfitz
	Cvar_RegisterVariable (&vid_width); //johnfitz
	Cvar_RegisterVariable (&vid_height); //johnfitz
	Cvar_RegisterVariable (&vid_bpp); //johnfitz
	Cvar_RegisterVariable (&vid_vsync); //johnfitz
	Cvar_RegisterVariable (&vid_filter);
	Cvar_RegisterVariable (&vid_anisotropic);
	Cvar_RegisterVariable (&vid_fsaa);
	Cvar_RegisterVariable (&vid_desktopfullscreen); //QuakeSpasm
	Cvar_RegisterVariable (&vid_borderless); //QuakeSpasm
	Cvar_SetCallback (&vid_fullscreen, VID_Changed_f);
	Cvar_SetCallback (&vid_width, VID_Changed_f);
	Cvar_SetCallback (&vid_height, VID_Changed_f);
	Cvar_SetCallback (&vid_bpp, VID_Changed_f);
	Cvar_SetCallback (&vid_filter, VID_FilterChanged_f);
	Cvar_SetCallback (&vid_anisotropic, VID_FilterChanged_f);
	Cvar_SetCallback (&vid_fsaa, VID_Changed_f);
	Cvar_SetCallback (&vid_vsync, VID_Changed_f);
	Cvar_SetCallback (&vid_desktopfullscreen, VID_Changed_f);
	Cvar_SetCallback (&vid_borderless, VID_Changed_f);
	
	Cmd_AddCommand ("vid_unlock", VID_Unlock); //johnfitz
	Cmd_AddCommand ("vid_restart", VID_Restart); //johnfitz
	Cmd_AddCommand ("vid_test", VID_Test); //johnfitz
	Cmd_AddCommand ("vid_describecurrentmode", VID_DescribeCurrentMode_f);
	Cmd_AddCommand ("vid_describemodes", VID_DescribeModes_f);

	putenv (vid_center);	/* SDL_putenv is problematic in versions <= 1.2.9 */

	if (SDL_InitSubSystem(SDL_INIT_VIDEO) < 0)
		Sys_Error("Couldn't init SDL video: %s", SDL_GetError());

	{
		SDL_DisplayMode mode;
		if (SDL_GetDesktopDisplayMode(0, &mode) != 0)
			Sys_Error("Could not get desktop display mode");

		display_width = mode.w;
		display_height = mode.h;
		display_bpp = SDL_BITSPERPIXEL(mode.format);
	}

	Cvar_SetValueQuick (&vid_bpp, (float)display_bpp);

	if (CFG_OpenConfig("config.cfg") == 0)
	{
		CFG_ReadCvars(read_vars, num_readvars);
		CFG_CloseConfig();
	}
	CFG_ReadCvarOverrides(read_vars, num_readvars);

	VID_InitModelist();

	width = (int)vid_width.value;
	height = (int)vid_height.value;
	bpp = (int)vid_bpp.value;
	fullscreen = (int)vid_fullscreen.value;

	if (COM_CheckParm("-current"))
	{
		width = display_width;
		height = display_height;
		bpp = display_bpp;
		fullscreen = true;
	}
	else
	{
		p = COM_CheckParm("-width");
		if (p && p < com_argc-1)
		{
			width = Q_atoi(com_argv[p+1]);

			if(!COM_CheckParm("-height"))
				height = width * 3 / 4;
		}

		p = COM_CheckParm("-height");
		if (p && p < com_argc-1)
		{
			height = Q_atoi(com_argv[p+1]);

			if(!COM_CheckParm("-width"))
				width = height * 4 / 3;
		}

		p = COM_CheckParm("-bpp");
		if (p && p < com_argc-1)
			bpp = Q_atoi(com_argv[p+1]);

		if (COM_CheckParm("-window") || COM_CheckParm("-w"))
			fullscreen = false;
		else if (COM_CheckParm("-fullscreen") || COM_CheckParm("-f"))
			fullscreen = true;
	}

	if (!VID_ValidMode(width, height, bpp, fullscreen))
	{
		width = (int)vid_width.value;
		height = (int)vid_height.value;
		bpp = (int)vid_bpp.value;
		fullscreen = (int)vid_fullscreen.value;
	}

	if (!VID_ValidMode(width, height, bpp, fullscreen))
	{
		width = 640;
		height = 480;
		bpp = display_bpp;
		fullscreen = false;
	}

	vid_initialized = true;

	vid.colormap = host_colormap;
	vid.fullbright = 256 - LittleLong (*((int *)vid.colormap + 2048));

	// set window icon
	PL_SetWindowIcon();

	VID_SetMode (width, height, bpp, fullscreen);

	Con_Printf("\nVulkan Initialization\n");
	GL_InitInstance();
	GL_InitDevice();
	GL_InitCommandBuffers();
	GL_CreateSwapChain();
	GL_CreateColorBuffer();
	GL_CreateDepthBuffer();
	GL_CreateRenderPasses();
	GL_CreateFrameBuffers();
	R_InitStagingBuffers();
	R_CreateDescriptorSetLayouts();
	R_CreateDescriptorPool();
	R_InitDynamicBuffers();
	R_InitSamplers();
	R_CreatePipelineLayouts();
	R_CreatePipelines();
	GL_CreateDescriptorSets();

	//johnfitz -- removed code creating "glquake" subdirectory

	vid_menucmdfn = VID_Menu_f; //johnfitz
	vid_menudrawfn = VID_MenuDraw;
	vid_menukeyfn = VID_MenuKey;

	VID_Gamma_Init(); //johnfitz
	VID_Menu_Init(); //johnfitz

	//QuakeSpasm: current vid settings should override config file settings.
	//so we have to lock the vid mode from now until after all config files are read.
	vid_locked = true;
}

/*
===================
VID_Restart -- johnfitz -- change video modes on the fly
===================
*/
static void VID_Restart (void)
{
	int width, height, bpp;
	qboolean fullscreen;

	if (vid_locked || !vid_changed)
		return;

	width = (int)vid_width.value;
	height = (int)vid_height.value;
	bpp = (int)vid_bpp.value;
	fullscreen = vid_fullscreen.value ? true : false;

	//
	// validate new mode
	//
	if (!VID_ValidMode (width, height, bpp, fullscreen))
	{
		Con_Printf ("%dx%dx%d %s is not a valid mode\n",
				width, height, bpp, fullscreen? "fullscreen" : "windowed");
		return;
	}

	scr_initialized = false;
	
	GL_WaitForDeviceIdle();
	R_DestroyPipelines();
	GL_DestroyBeforeSetMode();

	//
	// set new mode
	//
	VID_SetMode (width, height, bpp, fullscreen);

	GL_CreateSwapChain();
	GL_CreateColorBuffer();
	GL_CreateDepthBuffer();
	GL_CreateRenderPasses();
	GL_CreateFrameBuffers();
	R_CreatePipelines();
	GL_CreateDescriptorSets();

	//conwidth and conheight need to be recalculated
	vid.conwidth = (scr_conwidth.value > 0) ? (int)scr_conwidth.value : (scr_conscale.value > 0) ? (int)(vid.width/scr_conscale.value) : vid.width;
	vid.conwidth = CLAMP (320, vid.conwidth, vid.width);
	vid.conwidth &= 0xFFFFFFF8;
	vid.conheight = vid.conwidth * vid.height / vid.width;
	//
	// keep cvars in line with actual mode
	//
	VID_SyncCvars();

	//
	// update mouse grab
	//
	if (key_dest == key_console || key_dest == key_menu)
	{
		if (modestate == MS_WINDOWED)
			IN_Deactivate(true);
		else if (modestate == MS_FULLSCREEN)
			IN_Activate();
	}

	scr_initialized = true;
}

// new proc by S.A., called by alt-return key binding.
void	VID_Toggle (void)
{
	// disabling the fast path completely because SDL_SetWindowFullscreen was changing
	// the window size on SDL2/WinXP and we weren't set up to handle it. --ericw
	//
	// TODO: Clear out the dead code, reinstate the fast path using SDL_SetWindowFullscreen
	// inside VID_SetMode, check window size to fix WinXP issue. This will
	// keep all the mode changing code in one place.
	static qboolean vid_toggle_works = false;
	qboolean toggleWorked;
	Uint32 flags = 0;

	S_ClearBuffer ();

	if (!vid_toggle_works)
		goto vrestart;
	else
	{
		// disabling the fast path because with SDL 1.2 it invalidates VBOs (using them
		// causes a crash, sugesting that the fullscreen toggle created a new GL context,
		// although texture objects remain valid for some reason).
		//
		// SDL2 does promise window resizes / fullscreen changes preserve the GL context,
		// so we could use the fast path with SDL2. --ericw
		vid_toggle_works = false;
		goto vrestart;
	}

	if (!VID_GetFullscreen())
	{
		flags = vid_desktopfullscreen.value ? SDL_WINDOW_FULLSCREEN_DESKTOP : SDL_WINDOW_FULLSCREEN;
	}

	toggleWorked = SDL_SetWindowFullscreen(draw_context, flags) == 0;

	if (toggleWorked)
	{
		Sbar_Changed ();	// Sbar seems to need refreshing

		modestate = VID_GetFullscreen() ? MS_FULLSCREEN : MS_WINDOWED;

		VID_SyncCvars();

		// update mouse grab
		if (key_dest == key_console || key_dest == key_menu)
		{
			if (modestate == MS_WINDOWED)
				IN_Deactivate(true);
			else if (modestate == MS_FULLSCREEN)
				IN_Activate();
		}
	}
	else
	{
		vid_toggle_works = false;
		Con_DPrintf ("SDL_WM_ToggleFullScreen failed, attempting VID_Restart\n");
	vrestart:
		Cvar_SetQuick (&vid_fullscreen, VID_GetFullscreen() ? "0" : "1");
		Cbuf_AddText ("vid_restart\n");
	}
}

/*
================
VID_SyncCvars -- johnfitz -- set vid cvars to match current video mode
================
*/
void VID_SyncCvars (void)
{
	if (draw_context)
	{
		if (!VID_GetDesktopFullscreen())
		{
			Cvar_SetValueQuick (&vid_width, VID_GetCurrentWidth());
			Cvar_SetValueQuick (&vid_height, VID_GetCurrentHeight());
		}
		Cvar_SetValueQuick (&vid_bpp, VID_GetCurrentBPP());
		Cvar_SetQuick (&vid_fullscreen, VID_GetFullscreen() ? "1" : "0");
	}

	vid_changed = false;
}

//==========================================================================
//
//  NEW VIDEO MENU -- johnfitz
//
//==========================================================================

enum {
	VID_OPT_MODE,
	VID_OPT_BPP,
	VID_OPT_FULLSCREEN,
	VID_OPT_VSYNC,
	VID_OPT_ANTIALIASING,
	VID_OPT_FILTER,
	VID_OPT_ANISOTROPY,
	VID_OPT_UNDERWATER,
	VID_OPT_TEST,
	VID_OPT_APPLY,
	VIDEO_OPTIONS_ITEMS
};

static int	video_options_cursor = 0;

typedef struct {
	int width,height;
} vid_menu_mode;

//TODO: replace these fixed-length arrays with hunk_allocated buffers
static vid_menu_mode vid_menu_modes[MAX_MODE_LIST];
static int vid_menu_nummodes = 0;

static int vid_menu_bpps[MAX_BPPS_LIST];
static int vid_menu_numbpps = 0;

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
			if (vid_menu_modes[j].width == w &&
				vid_menu_modes[j].height == h)
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
VID_Menu_RebuildBppList

regenerates bpp list based on current vid_width and vid_height
================
*/
static void VID_Menu_RebuildBppList (void)
{
	int i, j, b;

	vid_menu_numbpps = 0;

	for (i = 0; i < nummodes; i++)
	{
		if (vid_menu_numbpps >= MAX_BPPS_LIST)
			break;

		//bpp list is limited to bpps available with current width/height
		if (modelist[i].width != vid_width.value ||
			modelist[i].height != vid_height.value)
			continue;

		b = modelist[i].bpp;

		for (j = 0; j < vid_menu_numbpps; j++)
		{
			if (vid_menu_bpps[j] == b)
				break;
		}

		if (j == vid_menu_numbpps)
		{
			vid_menu_bpps[j] = b;
			vid_menu_numbpps++;
		}
	}

	//if there are no valid fullscreen bpps for this width/height, just pick one
	if (vid_menu_numbpps == 0)
	{
		Cvar_SetValueQuick (&vid_bpp, (float)modelist[0].bpp);
		return;
	}

	//if vid_bpp is not in the new list, change vid_bpp
	for (i = 0; i < vid_menu_numbpps; i++)
		if (vid_menu_bpps[i] == (int)(vid_bpp.value))
			break;

	if (i == vid_menu_numbpps)
		Cvar_SetValueQuick (&vid_bpp, (float)vid_menu_bpps[0]);
}

/*
================
VID_Menu_ChooseNextMode

chooses next resolution in order, then updates vid_width and
vid_height cvars, then updates bpp and refreshrate lists
================
*/
static void VID_Menu_ChooseNextMode (int dir)
{
	int i;

	if (vid_menu_nummodes)
	{
		for (i = 0; i < vid_menu_nummodes; i++)
		{
			if (vid_menu_modes[i].width == vid_width.value &&
				vid_menu_modes[i].height == vid_height.value)
				break;
		}

		if (i == vid_menu_nummodes) //can't find it in list, so it must be a custom windowed res
		{
			i = 0;
		}
		else
		{
			i += dir;
			if (i >= vid_menu_nummodes)
				i = 0;
			else if (i < 0)
				i = vid_menu_nummodes-1;
		}

		Cvar_SetValueQuick (&vid_width, (float)vid_menu_modes[i].width);
		Cvar_SetValueQuick (&vid_height, (float)vid_menu_modes[i].height);
		VID_Menu_RebuildBppList ();
	}
}

/*
================
VID_Menu_ChooseNextBpp

chooses next bpp in order, then updates vid_bpp cvar
================
*/
static void VID_Menu_ChooseNextBpp (int dir)
{
	int i;

	if (vid_menu_numbpps)
	{
		for (i = 0; i < vid_menu_numbpps; i++)
		{
			if (vid_menu_bpps[i] == vid_bpp.value)
				break;
		}

		if (i == vid_menu_numbpps) //can't find it in list
		{
			i = 0;
		}
		else
		{
			i += dir;
			if (i >= vid_menu_numbpps)
				i = 0;
			else if (i < 0)
				i = vid_menu_numbpps-1;
		}

		Cvar_SetValueQuick (&vid_bpp, (float)vid_menu_bpps[i]);
	}
}

/*
================
VID_Menu_ChooseNextAAMode
================
*/
static void VID_Menu_ChooseNextAAMode(int dir)
{
	if(vulkan_physical_device_features.sampleRateShading) {
		Cvar_SetValueQuick(&vid_fsaa, (float)(((int)vid_fsaa.value + 3 + dir) % 3));
	} else {
		Cvar_SetValueQuick(&vid_fsaa, (float)(((int)vid_fsaa.value + 2 + dir) % 2));
	}
}

/*
================
VID_Menu_ChooseNextWaterWarp
================
*/
static void VID_Menu_ChooseNextWaterWarp (int dir)
{
	Cvar_SetValueQuick(&r_waterwarp, (float)(((int)r_waterwarp.value + 3 + dir) % 3));
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
	case K_ESCAPE:
		VID_SyncCvars (); //sync cvars before leaving menu. FIXME: there are other ways to leave menu
		S_LocalSound ("misc/menu1.wav");
		M_Menu_Options_f ();
		break;

	case K_UPARROW:
		S_LocalSound ("misc/menu1.wav");
		video_options_cursor--;
		if (video_options_cursor < 0)
			video_options_cursor = VIDEO_OPTIONS_ITEMS-1;
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
			VID_Menu_ChooseNextBpp (1);
			break;
		case VID_OPT_FULLSCREEN:
			Cbuf_AddText ("toggle vid_fullscreen\n");
			break;
		case VID_OPT_VSYNC:
			Cbuf_AddText ("toggle vid_vsync\n"); // kristian
			break;
		case VID_OPT_ANTIALIASING:
			VID_Menu_ChooseNextAAMode (-1);
			break;
		case VID_OPT_FILTER:
			Cbuf_AddText ("toggle vid_filter\n");
			break;
		case VID_OPT_ANISOTROPY:
			Cbuf_AddText ("toggle vid_anisotropic\n");
			break;
		case VID_OPT_UNDERWATER:
			VID_Menu_ChooseNextWaterWarp (-1);
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
			VID_Menu_ChooseNextBpp (-1);
			break;
		case VID_OPT_FULLSCREEN:
			Cbuf_AddText ("toggle vid_fullscreen\n");
			break;
		case VID_OPT_VSYNC:
			Cbuf_AddText ("toggle vid_vsync\n");
			break;
		case VID_OPT_ANTIALIASING:
			VID_Menu_ChooseNextAAMode(1);
			break;
		case VID_OPT_FILTER:
			Cbuf_AddText ("toggle vid_filter\n");
			break;
		case VID_OPT_ANISOTROPY:
			Cbuf_AddText ("toggle vid_anisotropic\n");
			break;
		case VID_OPT_UNDERWATER:
			VID_Menu_ChooseNextWaterWarp (1);
			break;
		default:
			break;
		}
		break;

	case K_ENTER:
	case K_KP_ENTER:
		m_entersound = true;
		switch (video_options_cursor)
		{
		case VID_OPT_MODE:
			VID_Menu_ChooseNextMode (1);
			break;
		case VID_OPT_BPP:
			VID_Menu_ChooseNextBpp (1);
			break;
		case VID_OPT_FULLSCREEN:
			Cbuf_AddText ("toggle vid_fullscreen\n");
			break;
		case VID_OPT_VSYNC:
			Cbuf_AddText ("toggle vid_vsync\n");
			break;
		case VID_OPT_ANTIALIASING:
			VID_Menu_ChooseNextAAMode(1);
			break;
		case VID_OPT_FILTER:
			Cbuf_AddText ("toggle vid_filter\n");
			break;
		case VID_OPT_ANISOTROPY:
			Cbuf_AddText ("toggle vid_anisotropic\n");
			break;
		case VID_OPT_UNDERWATER:
			VID_Menu_ChooseNextWaterWarp (1);
			break;
		case VID_OPT_TEST:
			Cbuf_AddText ("vid_test\n");
			break;
		case VID_OPT_APPLY:
			Cbuf_AddText ("vid_restart\n");
			key_dest = key_game;
			m_state = m_none;
			IN_Activate();
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
static void VID_MenuDraw (void)
{
	int i, y;
	qpic_t *p;
	const char *title;

	y = 4;

	// plaque
	p = Draw_CachePic ("gfx/qplaque.lmp");
	M_DrawTransPic (16, y, p);

	//p = Draw_CachePic ("gfx/vidmodes.lmp");
	p = Draw_CachePic ("gfx/p_option.lmp");
	M_DrawPic ( (320-p->width)/2, y, p);

	y += 28;

	// title
	title = "Video Options";
	M_PrintWhite ((320-8*strlen(title))/2, y, title);

	y += 16;

	// options
	for (i = 0; i < VIDEO_OPTIONS_ITEMS; i++)
	{
		switch (i)
		{
		case VID_OPT_MODE:
			M_Print (16, y, "        Video mode");
			M_Print (184, y, va("%ix%i", (int)vid_width.value, (int)vid_height.value));
			break;
		case VID_OPT_BPP:
			M_Print (16, y, "       Color depth");
			M_Print (184, y, va("%i", (int)vid_bpp.value));
			break;
		case VID_OPT_FULLSCREEN:
			M_Print (16, y, "        Fullscreen");
			M_DrawCheckbox (184, y, (int)vid_fullscreen.value);
			break;
		case VID_OPT_VSYNC:
			M_Print (16, y, "     Vertical sync");
			M_DrawCheckbox (184, y, (int)vid_vsync.value);
			break;
		case VID_OPT_ANTIALIASING:
			M_Print (16, y, "      Antialiasing");
			M_Print (184, y, ((int)vid_fsaa.value == 0) ? "off" : (((int)vid_fsaa.value == 1) ? "Multisample" : "Supersample"));
			break;
		case VID_OPT_FILTER:
			M_Print (16, y, "            Filter");
			M_Print (184, y, ((int)vid_filter.value == 0) ? "smooth" : "classic");
			break;
		case VID_OPT_ANISOTROPY:
			M_Print (16, y, "       Anisotropic");
			M_Print (184, y, ((int)vid_anisotropic.value == 0) ? "off" : "on");
			break;
		case VID_OPT_UNDERWATER:
			M_Print (16, y, "     Underwater FX");
			M_Print (184, y, ((int)r_waterwarp.value == 0) ? "off" : (((int)r_waterwarp.value == 1)  ? "Classic" : "glQuake"));
			break;
		case VID_OPT_TEST:
			y += 8; //separate the test and apply items
			M_Print (16, y, "      Test changes");
			break;
		case VID_OPT_APPLY:
			M_Print (16, y, "     Apply changes");
			break;
		}

		if (video_options_cursor == i)
			M_DrawCharacter (168, y, 12+((int)(realtime*4)&1));

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
	IN_Deactivate(modestate == MS_WINDOWED);
	key_dest = key_menu;
	m_state = m_video;
	m_entersound = true;

	//set all the cvars to match the current mode when entering the menu
	VID_SyncCvars ();

	//set up bpp and rate lists based on current cvars
	VID_Menu_RebuildBppList ();
}

/*
==============================================================================

SCREEN SHOTS

==============================================================================
*/

/*
==================
SCR_ScreenShot_f -- johnfitz -- rewritten to use Image_WriteTGA
==================
*/
void SCR_ScreenShot_f (void)
{
	VkBuffer buffer;
	VkResult err;
	char	tganame[16];  //johnfitz -- was [80]
	char	checkname[MAX_OSPATH];
	int	i;

	qboolean bgra = (vulkan_globals.swap_chain_format == VK_FORMAT_B8G8R8A8_UNORM)
		|| (vulkan_globals.swap_chain_format == VK_FORMAT_B8G8R8A8_SRGB);

	if ((vulkan_globals.swap_chain_format != VK_FORMAT_B8G8R8A8_UNORM)
		&& (vulkan_globals.swap_chain_format != VK_FORMAT_B8G8R8A8_SRGB)
		&& (vulkan_globals.swap_chain_format != VK_FORMAT_R8G8B8A8_UNORM)
		&& (vulkan_globals.swap_chain_format != VK_FORMAT_R8G8B8A8_SRGB))
	{
		Con_Printf ("SCR_ScreenShot_f: Unsupported surface format\n");
		return;
	}

// find a file name to save it to
	for (i=0; i<10000; i++)
	{
		q_snprintf (tganame, sizeof(tganame), "vkquake%04i.tga", i);	// "fitz%04i.tga"
		q_snprintf (checkname, sizeof(checkname), "%s/%s", com_gamedir, tganame);
		if (Sys_FileTime(checkname) == -1)
			break;	// file doesn't exist
	}
	if (i == 10000)
	{
		Con_Printf ("SCR_ScreenShot_f: Couldn't find an unused filename\n");
		return;
	}

// get data
	VkBufferCreateInfo buffer_create_info;
	memset(&buffer_create_info, 0, sizeof(buffer_create_info));
	buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	buffer_create_info.size = glwidth * glheight * 4;
	buffer_create_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	err = vkCreateBuffer(vulkan_globals.device, &buffer_create_info, NULL, &buffer);
	if (err != VK_SUCCESS)
		Sys_Error("vkCreateBuffer failed");

	VkMemoryRequirements memory_requirements;
	vkGetBufferMemoryRequirements(vulkan_globals.device, buffer, &memory_requirements);

	uint32_t memory_type_index = GL_MemoryTypeFromProperties(memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, VK_MEMORY_PROPERTY_HOST_CACHED_BIT);

	VkMemoryAllocateInfo memory_allocate_info;
	memset(&memory_allocate_info, 0, sizeof(memory_allocate_info));
	memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	memory_allocate_info.allocationSize = memory_requirements.size;
	memory_allocate_info.memoryTypeIndex = memory_type_index;

	VkDeviceMemory memory;
	err = vkAllocateMemory(vulkan_globals.device, &memory_allocate_info, NULL, &memory);
	if (err != VK_SUCCESS)
		Sys_Error("vkAllocateMemory failed");

	err = vkBindBufferMemory(vulkan_globals.device, buffer, memory, 0);
	if (err != VK_SUCCESS)
		Sys_Error("vkBindBufferMemory failed");

	VkCommandBuffer command_buffer;

	VkCommandBufferAllocateInfo command_buffer_allocate_info;
	memset(&command_buffer_allocate_info, 0, sizeof(command_buffer_allocate_info));
	command_buffer_allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	command_buffer_allocate_info.commandPool = transient_command_pool;
	command_buffer_allocate_info.commandBufferCount = 1;
	err = vkAllocateCommandBuffers(vulkan_globals.device, &command_buffer_allocate_info, &command_buffer);
	if (err != VK_SUCCESS)
		Sys_Error("vkAllocateCommandBuffers failed");

	VkCommandBufferBeginInfo command_buffer_begin_info;
	memset(&command_buffer_begin_info, 0, sizeof(command_buffer_begin_info));
	command_buffer_begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	err = vkBeginCommandBuffer(command_buffer, &command_buffer_begin_info);
	if (err != VK_SUCCESS)
		Sys_Error("vkBeginCommandBuffer failed");

	VkImageMemoryBarrier image_barrier;
	image_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	image_barrier.pNext = NULL;
	image_barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	image_barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	image_barrier.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	image_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	image_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	image_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	image_barrier.image = swapchain_images[current_command_buffer];
	image_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	image_barrier.subresourceRange.baseMipLevel = 0;
	image_barrier.subresourceRange.levelCount = 1;
	image_barrier.subresourceRange.baseArrayLayer = 0;
	image_barrier.subresourceRange.layerCount = 1;

	vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &image_barrier);

	VkBufferImageCopy image_copy;
	memset(&image_copy, 0, sizeof(image_copy));
	image_copy.bufferOffset = 0;
	image_copy.bufferRowLength = glwidth;
	image_copy.bufferImageHeight = glheight;
	image_copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	image_copy.imageSubresource.layerCount = 1;
	image_copy.imageExtent.width = glwidth;
	image_copy.imageExtent.height = glheight;
	image_copy.imageExtent.depth = 1;

	vkCmdCopyImageToBuffer(command_buffer, swapchain_images[current_command_buffer], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1, &image_copy);

	err = vkEndCommandBuffer(command_buffer);
	if (err != VK_SUCCESS)
		Sys_Error("vkEndCommandBuffer failed");

	VkSubmitInfo submit_info;
	memset(&submit_info, 0, sizeof(submit_info));
	submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers = &command_buffer;

	err = vkQueueSubmit(vulkan_globals.queue, 1, &submit_info, VK_NULL_HANDLE);
	if (err != VK_SUCCESS)
		Sys_Error("vkQueueSubmit failed");

	err = vkDeviceWaitIdle(vulkan_globals.device);
	if (err != VK_SUCCESS)
		Sys_Error("vkDeviceWaitIdle failed");

	void * buffer_ptr;
	vkMapMemory(vulkan_globals.device, memory, 0, glwidth * glheight * 4, 0, &buffer_ptr);

// now write the file
	if (Image_WriteTGA (tganame, buffer_ptr, glwidth, glheight, 32, true, bgra))
		Con_Printf ("Wrote %s\n", tganame);
	else
		Con_Printf ("SCR_ScreenShot_f: Couldn't create a TGA file\n");

	vkUnmapMemory(vulkan_globals.device, memory);
	vkFreeMemory(vulkan_globals.device, memory, NULL);
	vkDestroyBuffer(vulkan_globals.device, buffer, NULL);
	vkFreeCommandBuffers(vulkan_globals.device, transient_command_pool, 1, &command_buffer);
}

