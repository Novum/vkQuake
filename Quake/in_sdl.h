void IN_StartupJoystick (void);
void IN_ShutdownJoystick (void);
void IN_BeginIgnoringMouseEvents (void);
void IN_EndIgnoringMouseEvents (void);

#ifdef USE_SDL3
extern SDL_Gamepad *joy_active_controller;
#else
extern SDL_GameController *joy_active_controller;
#endif

static inline int IN_SDL_ScancodeToQuakeKey (SDL_Scancode scancode)
{
	switch (scancode)
	{
	case SDL_SCANCODE_TAB:
		return K_TAB;
	case SDL_SCANCODE_RETURN:
		return K_ENTER;
	case SDL_SCANCODE_RETURN2:
		return K_ENTER;
	case SDL_SCANCODE_ESCAPE:
		return K_ESCAPE;
	case SDL_SCANCODE_SPACE:
		return K_SPACE;

	case SDL_SCANCODE_A:
		return 'a';
	case SDL_SCANCODE_B:
		return 'b';
	case SDL_SCANCODE_C:
		return 'c';
	case SDL_SCANCODE_D:
		return 'd';
	case SDL_SCANCODE_E:
		return 'e';
	case SDL_SCANCODE_F:
		return 'f';
	case SDL_SCANCODE_G:
		return 'g';
	case SDL_SCANCODE_H:
		return 'h';
	case SDL_SCANCODE_I:
		return 'i';
	case SDL_SCANCODE_J:
		return 'j';
	case SDL_SCANCODE_K:
		return 'k';
	case SDL_SCANCODE_L:
		return 'l';
	case SDL_SCANCODE_M:
		return 'm';
	case SDL_SCANCODE_N:
		return 'n';
	case SDL_SCANCODE_O:
		return 'o';
	case SDL_SCANCODE_P:
		return 'p';
	case SDL_SCANCODE_Q:
		return 'q';
	case SDL_SCANCODE_R:
		return 'r';
	case SDL_SCANCODE_S:
		return 's';
	case SDL_SCANCODE_T:
		return 't';
	case SDL_SCANCODE_U:
		return 'u';
	case SDL_SCANCODE_V:
		return 'v';
	case SDL_SCANCODE_W:
		return 'w';
	case SDL_SCANCODE_X:
		return 'x';
	case SDL_SCANCODE_Y:
		return 'y';
	case SDL_SCANCODE_Z:
		return 'z';

	case SDL_SCANCODE_1:
		return '1';
	case SDL_SCANCODE_2:
		return '2';
	case SDL_SCANCODE_3:
		return '3';
	case SDL_SCANCODE_4:
		return '4';
	case SDL_SCANCODE_5:
		return '5';
	case SDL_SCANCODE_6:
		return '6';
	case SDL_SCANCODE_7:
		return '7';
	case SDL_SCANCODE_8:
		return '8';
	case SDL_SCANCODE_9:
		return '9';
	case SDL_SCANCODE_0:
		return '0';

	case SDL_SCANCODE_MINUS:
		return '-';
	case SDL_SCANCODE_EQUALS:
		return '=';
	case SDL_SCANCODE_LEFTBRACKET:
		return '[';
	case SDL_SCANCODE_RIGHTBRACKET:
		return ']';
	case SDL_SCANCODE_BACKSLASH:
		return '\\';
	case SDL_SCANCODE_NONUSHASH:
		return '#';
	case SDL_SCANCODE_SEMICOLON:
		return ';';
	case SDL_SCANCODE_APOSTROPHE:
		return '\'';
	case SDL_SCANCODE_GRAVE:
		return '`';
	case SDL_SCANCODE_COMMA:
		return ',';
	case SDL_SCANCODE_PERIOD:
		return '.';
	case SDL_SCANCODE_SLASH:
		return '/';
	case SDL_SCANCODE_NONUSBACKSLASH:
		return '\\';

	case SDL_SCANCODE_BACKSPACE:
		return K_BACKSPACE;
	case SDL_SCANCODE_UP:
		return K_UPARROW;
	case SDL_SCANCODE_DOWN:
		return K_DOWNARROW;
	case SDL_SCANCODE_LEFT:
		return K_LEFTARROW;
	case SDL_SCANCODE_RIGHT:
		return K_RIGHTARROW;

	case SDL_SCANCODE_LALT:
		return K_ALT;
	case SDL_SCANCODE_RALT:
		return K_ALT;
	case SDL_SCANCODE_LCTRL:
		return K_CTRL;
	case SDL_SCANCODE_RCTRL:
		return K_CTRL;
	case SDL_SCANCODE_LSHIFT:
		return K_SHIFT;
	case SDL_SCANCODE_RSHIFT:
		return K_SHIFT;

	case SDL_SCANCODE_F1:
		return K_F1;
	case SDL_SCANCODE_F2:
		return K_F2;
	case SDL_SCANCODE_F3:
		return K_F3;
	case SDL_SCANCODE_F4:
		return K_F4;
	case SDL_SCANCODE_F5:
		return K_F5;
	case SDL_SCANCODE_F6:
		return K_F6;
	case SDL_SCANCODE_F7:
		return K_F7;
	case SDL_SCANCODE_F8:
		return K_F8;
	case SDL_SCANCODE_F9:
		return K_F9;
	case SDL_SCANCODE_F10:
		return K_F10;
	case SDL_SCANCODE_F11:
		return K_F11;
	case SDL_SCANCODE_F12:
		return K_F12;
	case SDL_SCANCODE_INSERT:
		return K_INS;
	case SDL_SCANCODE_DELETE:
		return K_DEL;
	case SDL_SCANCODE_PAGEDOWN:
		return K_PGDN;
	case SDL_SCANCODE_PAGEUP:
		return K_PGUP;
	case SDL_SCANCODE_HOME:
		return K_HOME;
	case SDL_SCANCODE_END:
		return K_END;

	case SDL_SCANCODE_NUMLOCKCLEAR:
		return K_KP_NUMLOCK;
	case SDL_SCANCODE_KP_DIVIDE:
		return K_KP_SLASH;
	case SDL_SCANCODE_KP_MULTIPLY:
		return K_KP_STAR;
	case SDL_SCANCODE_KP_MINUS:
		return K_KP_MINUS;
	case SDL_SCANCODE_KP_7:
		return K_KP_HOME;
	case SDL_SCANCODE_KP_8:
		return K_KP_UPARROW;
	case SDL_SCANCODE_KP_9:
		return K_KP_PGUP;
	case SDL_SCANCODE_KP_PLUS:
		return K_KP_PLUS;
	case SDL_SCANCODE_KP_4:
		return K_KP_LEFTARROW;
	case SDL_SCANCODE_KP_5:
		return K_KP_5;
	case SDL_SCANCODE_KP_6:
		return K_KP_RIGHTARROW;
	case SDL_SCANCODE_KP_1:
		return K_KP_END;
	case SDL_SCANCODE_KP_2:
		return K_KP_DOWNARROW;
	case SDL_SCANCODE_KP_3:
		return K_KP_PGDN;
	case SDL_SCANCODE_KP_ENTER:
		return K_KP_ENTER;
	case SDL_SCANCODE_KP_0:
		return K_KP_INS;
	case SDL_SCANCODE_KP_PERIOD:
		return K_KP_DEL;

	case SDL_SCANCODE_LGUI:
		return K_COMMAND;
	case SDL_SCANCODE_RGUI:
		return K_COMMAND;

	case SDL_SCANCODE_PAUSE:
		return K_PAUSE;

	default:
		return 0;
	}
}

static const int buttonremap[] = {
	K_MOUSE1, /* left button		*/
	K_MOUSE3, /* middle button	*/
	K_MOUSE2, /* right button		*/
	K_MOUSE4, /* back button		*/
	K_MOUSE5  /* forward button	*/
};
