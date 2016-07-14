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

#define MAX_MODE_LIST	600 //johnfitz -- was 30
#define MAX_BPPS_LIST	5
#define WARP_WIDTH		320
#define WARP_HEIGHT		200
#define MAXWIDTH		10000
#define MAXHEIGHT		10000

#define NUM_COMMAND_BUFFERS 2

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

static void ClearAllStates (void);
static void GL_InitInstance (void);
static void GL_InitDevice (void);
static void GL_CreateRenderTargets(void);
static void GL_DestroyRenderTargets(void);

viddef_t	vid;				// global video state
modestate_t	modestate = MS_UNINIT;
qboolean	scr_skipupdate;

//====================================

//johnfitz -- new cvars
static cvar_t	vid_fullscreen = {"vid_fullscreen", "0", CVAR_ARCHIVE};	// QuakeSpasm, was "1"
static cvar_t	vid_width = {"vid_width", "800", CVAR_ARCHIVE};		// QuakeSpasm, was 640
static cvar_t	vid_height = {"vid_height", "600", CVAR_ARCHIVE};	// QuakeSpasm, was 480
static cvar_t	vid_bpp = {"vid_bpp", "16", CVAR_ARCHIVE};
static cvar_t	vid_vsync = {"vid_vsync", "0", CVAR_ARCHIVE};
static cvar_t	vid_fsaa = {"vid_fsaa", "0", CVAR_ARCHIVE}; // QuakeSpasm
static cvar_t	vid_desktopfullscreen = {"vid_desktopfullscreen", "0", CVAR_ARCHIVE}; // QuakeSpasm
//johnfitz

cvar_t		vid_gamma = {"gamma", "1", CVAR_ARCHIVE}; //johnfitz -- moved here from view.c

// Vulkan
static VkInstance vulkan_instance;
static VkPhysicalDevice vulkan_physical_device;
static VkSurfaceKHR vulkan_surface;
static VkSurfaceCapabilitiesKHR vulkan_surface_capabilities;
static VkSwapchainKHR vulkan_swapchain;

static uint32_t vulkan_current_command_buffers;
static VkCommandPool vulkan_command_pool;
static VkCommandBuffer vulkan_command_buffers[2];
static VkFence vulkan_command_buffer_fences[2];
static qboolean vulkan_command_buffer_submitted[2];
static VkFramebuffer vulkan_framebuffers[2];
static VkImageView swapchain_images_views[2];

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
VID_Gamma_f -- callback when the cvar changes
================
*/
static void VID_Gamma_f (cvar_t *var)
{
}

/*
================
VID_Gamma_Init -- call on init
================
*/
static void VID_Gamma_Init (void)
{
	Cvar_RegisterVariable (&vid_gamma);
	Cvar_SetCallback (&vid_gamma, VID_Gamma_f);
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

		draw_context = SDL_CreateWindow (caption, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, width, height, flags);
		if (!draw_context)
			Sys_Error ("Couldn't create window");

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
	
// ericw -- OS X, SDL1: textures, VBO's invalid after mode change
//          OS X, SDL2: still valid after mode change
// To handle both cases, delete all GL objects (textures, VBO, GLSL) now.
// We must not interleave deleting the old objects with creating new ones, because
// one of the new objects could be given the same ID as an invalid handle
// which is later deleted.

	GL_DestroyRenderTargets();
	TexMgr_DeleteTextureObjects ();
	GLSLGamma_DeleteTexture ();
	R_DeleteShaders ();
	GL_DeleteBModelVertexBuffer ();
	GLMesh_DeleteVertexBuffers ();

//
// set new mode
//
	VID_SetMode (width, height, bpp, fullscreen);

	GL_CreateRenderTargets();
	TexMgr_ReloadImages ();
	GL_BuildBModelVertexBuffer ();
	GLMesh_LoadVertexBuffers ();
	Fog_SetupState ();

	//warpimages needs to be recalculated
	TexMgr_RecalcWarpImageSize ();

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
GL_InitInstance
===============
*/
static void GL_InitInstance( void )
{
	VkResult err;

	int found_surface_extensions = 0;

	uint32_t instance_extension_count;
	err = vkEnumerateInstanceExtensionProperties(NULL, &instance_extension_count, NULL);
	if (err == VK_SUCCESS || instance_extension_count > 0)
	{
		VkExtensionProperties *instance_extensions = malloc(sizeof(VkExtensionProperties) * instance_extension_count);
		err = vkEnumerateInstanceExtensionProperties(NULL, &instance_extension_count, instance_extensions);

		for (uint32_t i = 0; i < instance_extension_count; ++i)
		{
			if (strcmp(VK_KHR_SURFACE_EXTENSION_NAME, instance_extensions[i].extensionName) == 0)
			{
				found_surface_extensions++;
			}
			if (strcmp(VK_KHR_WIN32_SURFACE_EXTENSION_NAME, instance_extensions[i].extensionName) == 0)
			{
				found_surface_extensions++;
			}
		}

		free(instance_extensions);
	}

	if(found_surface_extensions != 2)
		Sys_Error("Couldn't find %s/%s extensions", VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
	
	VkApplicationInfo application_info;
	memset(&application_info, 0, sizeof(application_info));
	application_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	application_info.pApplicationName = "vkQuake";
	application_info.applicationVersion = 1;
	application_info.pEngineName = "vkQuake";
	application_info.engineVersion = 1;
	application_info.apiVersion = VK_API_VERSION_1_0;

	char * instance_extensions[] = { VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME };
	char * layer_names[] = { "VK_LAYER_LUNARG_standard_validation" };

	VkInstanceCreateInfo instance_create_info;
	memset(&instance_create_info, 0, sizeof(instance_create_info));
	instance_create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	instance_create_info.pApplicationInfo = &application_info;
	instance_create_info.enabledExtensionCount = 2;
	instance_create_info.ppEnabledExtensionNames = instance_extensions;
#ifdef _DEBUG
	instance_create_info.enabledLayerCount = 1;
	instance_create_info.ppEnabledLayerNames = layer_names;
#endif

	err = vkCreateInstance(&instance_create_info, NULL, &vulkan_instance);
	if (err != VK_SUCCESS)
		Sys_Error("Couldn't create Vulkan instance");

	VkWin32SurfaceCreateInfoKHR surface_create_info;
	memset(&surface_create_info, 0, sizeof(surface_create_info));
	surface_create_info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
	surface_create_info.hinstance = GetModuleHandle(NULL);
	surface_create_info.hwnd = sys_wm_info.info.win.window;

	err = vkCreateWin32SurfaceKHR(vulkan_instance, &surface_create_info, NULL, &vulkan_surface);
	if (err != VK_SUCCESS)
		Sys_Error("Couldn't create Vulkan surface");

	GET_INSTANCE_PROC_ADDR(vulkan_instance, GetDeviceProcAddr);
	GET_INSTANCE_PROC_ADDR(vulkan_instance, GetPhysicalDeviceSurfaceSupportKHR);
	GET_INSTANCE_PROC_ADDR(vulkan_instance, GetPhysicalDeviceSurfaceCapabilitiesKHR);
	GET_INSTANCE_PROC_ADDR(vulkan_instance, GetPhysicalDeviceSurfaceFormatsKHR);
	GET_INSTANCE_PROC_ADDR(vulkan_instance, GetPhysicalDeviceSurfacePresentModesKHR);
	GET_INSTANCE_PROC_ADDR(vulkan_instance, GetSwapchainImagesKHR);
}

/*
===============
GL_InitDevice
===============
*/
static void GL_InitDevice( void )
{
	VkResult err;

	uint32_t physical_device_count;
	err = vkEnumeratePhysicalDevices(vulkan_instance, &physical_device_count, NULL);
	if (err != VK_SUCCESS || physical_device_count == 0)
		Sys_Error("Couldn't find any Vulkan devices");

	VkPhysicalDevice *physical_devices = malloc(sizeof(VkPhysicalDevice) * physical_device_count);
	err = vkEnumeratePhysicalDevices(vulkan_instance, &physical_device_count, physical_devices);
	vulkan_physical_device = physical_devices[0];
	free(physical_devices);

	qboolean found_swapchain_extension = false;

	vkGetPhysicalDeviceMemoryProperties(vulkan_physical_device, &vulkan_globals.memory_properties);

	uint32_t device_extension_count;
	err = vkEnumerateDeviceExtensionProperties(vulkan_physical_device, NULL, &device_extension_count, NULL);

	if (err == VK_SUCCESS || device_extension_count > 0)
	{
		VkExtensionProperties *device_extensions = malloc(sizeof(VkExtensionProperties) * device_extension_count);
		err = vkEnumerateDeviceExtensionProperties(vulkan_physical_device, NULL, &device_extension_count, device_extensions);

		for (uint32_t i = 0; i < device_extension_count; ++i)
		{
			if (strcmp(VK_KHR_SWAPCHAIN_EXTENSION_NAME, device_extensions[i].extensionName) == 0)
			{
				found_swapchain_extension = true;
				break;
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
	for (uint32_t i = 0; i < vulkan_queue_count; ++i)
	{
		if ((queue_family_properties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0)
		{
			found_graphics_queue = true;
			vulkan_globals.gfx_queue_family_index = i;
			break;
		}
	}

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

	char * device_extensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
	char * layer_names[] = { "VK_LAYER_LUNARG_standard_validation" };

	VkDeviceCreateInfo device_create_info;
	memset(&device_create_info, 0, sizeof(device_create_info));
	device_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	device_create_info.queueCreateInfoCount = 1;
	device_create_info.pQueueCreateInfos = &queue_create_info;
	device_create_info.enabledExtensionCount = 1;
	device_create_info.ppEnabledExtensionNames = device_extensions;
#ifdef _DEBUG
	device_create_info.enabledLayerCount = 1;
	device_create_info.ppEnabledLayerNames = layer_names;
#endif

	err = vkCreateDevice(vulkan_physical_device, &device_create_info, NULL, &vulkan_globals.device);
	if (err != VK_SUCCESS)
		Sys_Error("Couldn't create Vulkan device");

	GET_DEVICE_PROC_ADDR(vulkan_globals.device, CreateSwapchainKHR);
	GET_DEVICE_PROC_ADDR(vulkan_globals.device, DestroySwapchainKHR);
	GET_DEVICE_PROC_ADDR(vulkan_globals.device, GetSwapchainImagesKHR);
	GET_DEVICE_PROC_ADDR(vulkan_globals.device, AcquireNextImageKHR);
	GET_DEVICE_PROC_ADDR(vulkan_globals.device, QueuePresentKHR);

	vkGetDeviceQueue(vulkan_globals.device, vulkan_globals.gfx_queue_family_index, 0, &vulkan_globals.queue);
}

/*
===============
GL_InitCommandBuffers
===============
*/
static void GL_InitCommandBuffers( void )
{
	Con_Printf("Creating command buffers\n");

	VkResult err;

	VkCommandPoolCreateInfo command_pool_create_info;
	memset(&command_pool_create_info, 0, sizeof(command_pool_create_info));
	command_pool_create_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	command_pool_create_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	command_pool_create_info.queueFamilyIndex = vulkan_globals.gfx_queue_family_index;

	err = vkCreateCommandPool(vulkan_globals.device, &command_pool_create_info, NULL, &vulkan_command_pool);
	if (err != VK_SUCCESS)
		Sys_Error("vkCreateCommandPool failed");

	VkCommandBufferAllocateInfo command_buffer_allocate_info;
	memset(&command_buffer_allocate_info, 0, sizeof(command_buffer_allocate_info));
	command_buffer_allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	command_buffer_allocate_info.commandPool = vulkan_command_pool;
	command_buffer_allocate_info.commandBufferCount = NUM_COMMAND_BUFFERS;

	err = vkAllocateCommandBuffers(vulkan_globals.device, &command_buffer_allocate_info, vulkan_command_buffers);
	if (err != VK_SUCCESS)
		Sys_Error("vkAllocateCommandBuffers failed");

	VkFenceCreateInfo fence_create_info;
	memset(&fence_create_info, 0, sizeof(fence_create_info));
	fence_create_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

	for (int i = 0; i < NUM_COMMAND_BUFFERS; ++i) 
	{
		err = vkCreateFence(vulkan_globals.device, &fence_create_info, NULL, &vulkan_command_buffer_fences[i]);
		if (err != VK_SUCCESS)
			Sys_Error("vkCreateFence failed");
	}
}

/*
====================
GL_CreateRenderPass
====================
*/
static void GL_CreateRenderPass()
{
	VkResult err;

	VkAttachmentDescription attachment_descriptions[1];
	memset(&attachment_descriptions, 0, sizeof(attachment_descriptions));

	attachment_descriptions[0].initialLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	attachment_descriptions[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	attachment_descriptions[0].samples = VK_SAMPLE_COUNT_1_BIT;
	attachment_descriptions[0].format = vulkan_globals.swap_chain_format;
	attachment_descriptions[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachment_descriptions[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;

	VkAttachmentReference attachment_references[1];
	memset(&attachment_references, 0, sizeof(attachment_references));

	attachment_references[0].attachment = 0;
	attachment_references[0].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkSubpassDescription subpass_descriptions[1];
	memset(&subpass_descriptions, 0, sizeof(subpass_descriptions));

	subpass_descriptions[0].colorAttachmentCount = 1;
	subpass_descriptions[0].pColorAttachments = attachment_references;
	subpass_descriptions[0].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;

	VkRenderPassCreateInfo render_pass_create_info;
	memset(&render_pass_create_info, 0, sizeof(render_pass_create_info));
	render_pass_create_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	render_pass_create_info.attachmentCount = 1;
	render_pass_create_info.pAttachments = attachment_descriptions;
	render_pass_create_info.subpassCount = 1;
	render_pass_create_info.pSubpasses = subpass_descriptions;

	err = vkCreateRenderPass(vulkan_globals.device, &render_pass_create_info, NULL, &vulkan_globals.render_pass);
	if (err != VK_SUCCESS)
		Sys_Error("Couldn't create Vulkan render pass");
}

/*
===============
GL_CreateRenderTargets
===============
*/
static void GL_CreateRenderTargets( void )
{
	Con_Printf("Creating render targets\n");

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
	swapchain_create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	swapchain_create_info.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
	swapchain_create_info.imageArrayLayers = 1;
	swapchain_create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	swapchain_create_info.queueFamilyIndexCount = 0;
	swapchain_create_info.pQueueFamilyIndices = NULL;
	swapchain_create_info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
	swapchain_create_info.clipped = true;
	swapchain_create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;

	vulkan_globals.swap_chain_format = surface_formats[0].format;
	free(surface_formats);

	err = fpCreateSwapchainKHR(vulkan_globals.device, &swapchain_create_info, NULL, &vulkan_swapchain);
	if (err != VK_SUCCESS)
		Sys_Error("Couldn't create swap chain");

	uint32_t image_count;
	err = fpGetSwapchainImagesKHR(vulkan_globals.device, vulkan_swapchain, &image_count, NULL);
	if (err != VK_SUCCESS || image_count != 2)
		Sys_Error("Couldn't get swap chain images");

	VkImage *swapchain_images = (VkImage *)malloc(image_count * sizeof(VkImage));
	fpGetSwapchainImagesKHR(vulkan_globals.device, vulkan_swapchain, &image_count, swapchain_images);

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

	VkFramebufferCreateInfo framebuffer_create_info;
	memset(&framebuffer_create_info, 0, sizeof(framebuffer_create_info));
	framebuffer_create_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	framebuffer_create_info.renderPass = vulkan_globals.render_pass;
	framebuffer_create_info.attachmentCount = 1;
	framebuffer_create_info.width = vid.width;
	framebuffer_create_info.height = vid.height;
	framebuffer_create_info.layers = 1;

	for (int i = 0; i < 2; ++i)
	{
		image_view_create_info.image = swapchain_images[i];
		err = vkCreateImageView(vulkan_globals.device, &image_view_create_info, NULL, &swapchain_images_views[i]);
		if (err != VK_SUCCESS)
			Sys_Error("vkCreateImageView failed");

		framebuffer_create_info.pAttachments = &swapchain_images_views[i];
		err = vkCreateFramebuffer(vulkan_globals.device, &framebuffer_create_info, NULL, &vulkan_framebuffers[i]);
		if (err != VK_SUCCESS)
			Sys_Error("vkCreateFramebuffer failed");
	}

	free(swapchain_images);
}

/*
===============
GL_DestroyRenderTargets
===============
*/
static void GL_DestroyRenderTargets( void )
{
	Con_Printf("Destroying render targets\n");

	for (int i = 0; i < 2; ++i)
	{
		vkDestroyImageView(vulkan_globals.device, swapchain_images_views[i], NULL);
		swapchain_images_views[i] = VK_NULL_HANDLE;
	}

	fpDestroySwapchainKHR(vulkan_globals.device, vulkan_swapchain, NULL);
}

/*
=================
GL_BeginRendering
=================
*/
void GL_BeginRendering (int *x, int *y, int *width, int *height)
{
	R_SubmitStagingBuffers();

	*x = *y = 0;
	*width = vid.width;
	*height = vid.height;

	VkResult err;

	if (vulkan_command_buffer_submitted[vulkan_current_command_buffers])
	{
		err = vkWaitForFences(vulkan_globals.device, 1, &vulkan_command_buffer_fences[vulkan_current_command_buffers], VK_TRUE, UINT64_MAX);
		if (err != VK_SUCCESS)
			Sys_Error("vkWaitForFences failed");
	}

	err = vkResetFences(vulkan_globals.device, 1, &vulkan_command_buffer_fences[vulkan_current_command_buffers]);
	if (err != VK_SUCCESS)
		Sys_Error("vkResetFences failed");

	VkCommandBufferBeginInfo command_buffer_begin_info;
	memset(&command_buffer_begin_info, 0, sizeof(command_buffer_begin_info));
	command_buffer_begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

	vulkan_globals.command_buffer = vulkan_command_buffers[vulkan_current_command_buffers];
	err = vkBeginCommandBuffer(vulkan_globals.command_buffer, &command_buffer_begin_info);
	if (err != VK_SUCCESS)
		Sys_Error("vkBeginCommandBuffer failed");

	err = fpAcquireNextImageKHR(vulkan_globals.device, vulkan_swapchain, UINT64_MAX, VK_NULL_HANDLE, VK_NULL_HANDLE, &current_swapchain_buffer);
	if (err != VK_SUCCESS)
		Sys_Error("Couldn't acquire next image");

	VkRect2D render_area;
	render_area.offset.x = 0;
	render_area.offset.y = 0;
	render_area.extent.width = vid.width;
	render_area.extent.height = vid.height;

	VkRenderPassBeginInfo render_pass_begin_info;
	memset(&render_pass_begin_info, 0, sizeof(render_pass_begin_info));
	render_pass_begin_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	render_pass_begin_info.renderArea = render_area;
	render_pass_begin_info.renderPass = vulkan_globals.render_pass;
	render_pass_begin_info.framebuffer = vulkan_framebuffers[vulkan_current_command_buffers];
	render_pass_begin_info.clearValueCount = 1;
	render_pass_begin_info.pClearValues = &vulkan_globals.clear_value;

	vkCmdBeginRenderPass(vulkan_globals.command_buffer, &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);
}

/*
=================
GL_EndRendering
=================
*/
void GL_EndRendering (void)
{
	VkResult err;

	vkCmdEndRenderPass(vulkan_globals.command_buffer);

	err = vkEndCommandBuffer(vulkan_globals.command_buffer);
	if (err != VK_SUCCESS)
		Sys_Error("vkEndCommandBuffer failed");

	VkSubmitInfo submit_info;
	memset(&submit_info, 0, sizeof(submit_info));
	submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers = &vulkan_command_buffers[vulkan_current_command_buffers];

	err = vkQueueSubmit(vulkan_globals.queue, 1, &submit_info, vulkan_command_buffer_fences[vulkan_current_command_buffers]);
	if (err != VK_SUCCESS)
		Sys_Error("vkQueueSubmit failed");

	vulkan_command_buffer_submitted[vulkan_current_command_buffers] = true;
	vulkan_current_command_buffers = (vulkan_current_command_buffers + 1) % NUM_COMMAND_BUFFERS;

	VkPresentInfoKHR present_info;
	memset(&present_info, 0, sizeof(present_info));
	present_info.swapchainCount = 1;
	present_info.pSwapchains = &vulkan_swapchain,
	present_info.pImageIndices = &current_swapchain_buffer;
	err = fpQueuePresentKHR(vulkan_globals.queue, &present_info);
	if (err != VK_SUCCESS)
		Sys_Error("vkQueuePresentKHR failed");
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

/*
===================
VID_FSAA_f -- ericw -- warn that vid_fsaa requires engine restart
===================
*/
static void VID_FSAA_f (cvar_t *var)
{
	// don't print the warning if vid_fsaa is set during startup
	if (vid_initialized)
		Con_Printf("%s %d requires engine restart to take effect\n", var->name, (int)var->value);
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
					 "vid_fsaa",
					 "vid_desktopfullscreen" };
#define num_readvars	( sizeof(read_vars)/sizeof(read_vars[0]) )

	Cvar_RegisterVariable (&vid_fullscreen); //johnfitz
	Cvar_RegisterVariable (&vid_width); //johnfitz
	Cvar_RegisterVariable (&vid_height); //johnfitz
	Cvar_RegisterVariable (&vid_bpp); //johnfitz
	Cvar_RegisterVariable (&vid_vsync); //johnfitz
	Cvar_RegisterVariable (&vid_fsaa); //QuakeSpasm
	Cvar_RegisterVariable (&vid_desktopfullscreen); //QuakeSpasm
	Cvar_SetCallback (&vid_fullscreen, VID_Changed_f);
	Cvar_SetCallback (&vid_width, VID_Changed_f);
	Cvar_SetCallback (&vid_height, VID_Changed_f);
	Cvar_SetCallback (&vid_bpp, VID_Changed_f);
	Cvar_SetCallback (&vid_vsync, VID_Changed_f);
	Cvar_SetCallback (&vid_fsaa, VID_FSAA_f);
	Cvar_SetCallback (&vid_desktopfullscreen, VID_Changed_f);

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

	vid.maxwarpwidth = WARP_WIDTH;
	vid.maxwarpheight = WARP_HEIGHT;
	vid.colormap = host_colormap;
	vid.fullbright = 256 - LittleLong (*((int *)vid.colormap + 2048));

	// set window icon
	PL_SetWindowIcon();

	VID_SetMode (width, height, bpp, fullscreen);

	Con_Printf("\nVulkan Initialization\n");
	GL_InitInstance();
	GL_InitDevice();
	GL_InitCommandBuffers();
	GL_CreateRenderPass();
	GL_CreateRenderTargets();
	R_InitStagingBuffers();

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
		Cvar_SetQuick (&vid_vsync, VID_GetVSync() ? "1" : "0");
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

