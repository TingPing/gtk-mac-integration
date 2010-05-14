/* GTK+ application-level integration for the Mac OS X/Cocoa 
 *
 * Copyright (C) 2007 Pioneer Research Center USA, Inc.
 * Copyright (C) 2007 Imendio AB
 * Copyright (C) 2009 Paul Davis
 *
 * This is a reimplementation in Cocoa of the sync-menu.c concept
 * from Imendio, although without the "set quit menu" API since
 * a Cocoa app needs to handle termination anyway.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; version 2.1
 * of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <gtk/gtk.h>

#import "GtkApplicationDelegate.h"
#import "GtkApplicationNotify.h"
#import "GNSMenuBar.h"
#import "GNSMenuItem.h"

#include "gtkosxapplication.h"
#include "gtkosxapplicationprivate.h"
#include "cocoa_menu_item.h"
#include "cocoa_menu.h"
#include "getlabel.h"
#include "ige-mac-image-utils.h"

/* This is a private function in libgdk; we need to have is so that we
   can force new windows onto the Window menu */
extern NSWindow* gdk_quartz_window_get_nswindow(GdkWindow*);

//#define DEBUG(format, ...) g_printerr ("%s: " format, G_STRFUNC, ## __VA_ARGS__)
#define DEBUG(format, ...)

/* TODO
 *
 * - Sync adding/removing/reordering items
 * - Create on demand? (can this be done with gtk+? ie fill in menu
 items when the menu is opened)
 * - Figure out what to do per app/window...
 *
 */
G_DEFINE_TYPE (GtkOSXApplication, gtk_osxapplication, G_TYPE_OBJECT)


static gulong emission_hook_id = 0;

/*
 * parent_set_emission_hook:
 * @ihint: The signal hint confgigured when the signal was created.
 * @n_param_values: The number of parameters passed in param_values
 * @param_values: A GValue[] containing the parameters
 * data: A gpointer to pass to the signal handler
 *
 * Sets an emission hook for all parent-set signals. 
 */
static gboolean
parent_set_emission_hook (GSignalInvocationHint *ihint,
			  guint                  n_param_values,
			  const GValue          *param_values,
			  gpointer               data)
{
  GtkWidget *instance = (GtkWidget*) g_value_get_object (param_values);

  if (GTK_IS_MENU_ITEM (instance)) {
    GtkWidget *previous_parent = (GtkWidget*) g_value_get_object (param_values + 1);
    GtkWidget *menu_shell      = NULL;

    if (GTK_IS_MENU_SHELL (previous_parent)) {
      menu_shell = previous_parent;
    }
    else if (GTK_IS_MENU_SHELL (instance->parent)) {
      menu_shell = instance->parent;
    }

    if (menu_shell) {
      GNSMenuBar *cocoa_menu = (GNSMenuBar*)cocoa_menu_get (menu_shell);
      if (GTK_IS_MENU_BAR(menu_shell) && cocoa_menu && 
	  [cocoa_menu respondsToSelector: @selector(resync)]) {
	[cocoa_menu resync];
      }
      else
	cocoa_menu_item_add_submenu (GTK_MENU_SHELL (menu_shell),
				     cocoa_menu,
				     cocoa_menu == (NSMenu*) data,
				     FALSE);
    }
  }
  return TRUE;
}

/*
 * parent_set_emission_hook_remove:
 * @widget: A random widget. Not used.
 * @data: A random gpointer. Not used.
 *
 * Removes the parent-set emission hook. This meets a particular
 * template, which is the reason for the unused parameters.
 */
static void
parent_set_emission_hook_remove (GtkWidget *widget,
				 gpointer   data)
{
  g_signal_remove_emission_hook (g_signal_lookup ("parent-set",
						  GTK_TYPE_WIDGET),
				 emission_hook_id);
}

/*
 * add_to_menubar:
 * @self: The GtkOSXApplication pointer.
 * @menu: The cocoa menu to add
 * pos: The position on the menubar to insert the new menu
 *
 * Add a submenu to the currently active main menubar.
 *
 * Returns: a pointer to the NSMenuItem now holding the menu.
 */
static GNSMenuItem*
add_to_menubar (GtkOSXApplication *self, NSMenu *menu, NSInteger pos)
{
  GNSMenuItem *dummyItem = [[GNSMenuItem alloc] initWithTitle:@""
					      action:nil keyEquivalent:@""];
  NSMenu *menubar = [NSApp mainMenu];

  [dummyItem setSubmenu:menu];
  if (pos < 0)
      [menubar addItem:dummyItem];
  else
      [menubar insertItem:dummyItem atIndex:pos];
  return dummyItem;
}

/*
 * create_apple_menu:
 * @self: The GtkOSXApplication object.
 *
 * Creates the "app" menu -- the first one on the menubar with the
 * application's name. The function is called create_apple_menu
 * because of the undocumented Cocoa method to set it on the mainMenu.
 *
 * Returns: A pointer to the menu item.
 */
static GNSMenuItem*
create_apple_menu (GtkOSXApplication *self)
{
  NSMenuItem *menuitem;
  // Create the application (Apple) menu.
  NSMenu *app_menu = [[NSMenu alloc] initWithTitle: @"Apple Menu"];

  NSMenu *menuServices = [[NSMenu alloc] initWithTitle: @"Services"];
  [NSApp setServicesMenu:menuServices];

  [app_menu addItem: [NSMenuItem separatorItem]];
  menuitem = [[NSMenuItem alloc] initWithTitle: @"Services"
				 action:nil keyEquivalent:@""];
  [menuitem setSubmenu:menuServices];
  [app_menu addItem: menuitem];
  [menuitem release];
  [app_menu addItem: [NSMenuItem separatorItem]];
  menuitem = [[NSMenuItem alloc] initWithTitle:@"Hide"
				 action:@selector(hide:) keyEquivalent:@""];
  [menuitem setTarget: NSApp];
  [app_menu addItem: menuitem];
  [menuitem release];
  menuitem = [[NSMenuItem alloc] initWithTitle:@"Hide Others"
				 action:@selector(hideOtherApplications:) keyEquivalent:@""];
  [menuitem setTarget: NSApp];
  [app_menu addItem: menuitem];
  [menuitem release];
  menuitem = [[NSMenuItem alloc] initWithTitle:@"Show All"
				 action:@selector(unhideAllApplications:) keyEquivalent:@""];
  [menuitem setTarget: NSApp];
  [app_menu addItem: menuitem];
  [menuitem release];
  [app_menu addItem: [NSMenuItem separatorItem]];
  menuitem = [[NSMenuItem alloc] initWithTitle:@"Quit"
				 action:@selector(terminate:) keyEquivalent:@"q"];
  [menuitem setTarget: NSApp];
  [app_menu addItem: menuitem];
  [menuitem release];

  [NSApp performSelector:@selector(setAppleMenu:) withObject:app_menu];
  return add_to_menubar (self, app_menu, 0);
}

/*
 * create_window_menu:
 * @self: The pointer to the GtkOSXApplication object
 * @window: The toplevel window for which the menu is being created
 *
 * Creates the Window menu, the one which tracks the application's windows.
 * FIXME: We need an API to suppress this for single-window applications
 *
 * Returns: A pointer to the menu item on the mainMenu.
 */
static GNSMenuItem *
create_window_menu (GtkOSXApplication *self, NSWindow* window)
{   
  NSMenu *window_menu = [[NSMenu alloc] initWithTitle: @"Window"];
  NSInteger pos;
  
  [window_menu addItemWithTitle:@"Minimize"
		action:@selector(performMiniaturize:) keyEquivalent:@""];
  [window_menu addItem: [NSMenuItem separatorItem]];
  [window_menu addItemWithTitle:@"Bring All to Front"
		action:@selector(arrangeInFront:) keyEquivalent:@""];

  [NSApp setWindowsMenu:window_menu];
  [NSApp addWindowsItem: window title: [window title] filename: NO];
  pos = [[NSApp mainMenu] indexOfItemWithTitle: @"Help"];
  return add_to_menubar (self, window_menu, pos);
}  

/*
 * gtk_osxapplication_constructor:
 * @gtype: The GType of the new class
 * @n_properties: The number of properties passed in the next parameter
 * @properties: an array of construction properties
 *
 * Overrides the GObject (superclass) constructor to enforce a singleton
 * The commented-out code is for thread safety, but doesn't yet work.
 *
 * Returns: A pointer to the new object.
 */
static GObject *
gtk_osxapplication_constructor (GType gtype,
			     guint n_properties,
			     GObjectConstructParam *properties)
{
  static GObject *self = NULL;
  //static GOnce once = G_ONCE_INIT;
  if (self == NULL)
    {
/*       struct construction_args args; */
/*       args.gtype = gtype; */
/*       args.n_props = n_properties; */
/*       args.props = properties; */
/*       g_once (&once, _parent_constructor, &args); */
/*       self = G_OBJECT_CLASS (once.retval); */
      self = G_OBJECT_CLASS(gtk_osxapplication_parent_class)->constructor(gtype, n_properties, properties);
      g_object_add_weak_pointer (self, (gpointer) &self);
      return self;
    }

  return g_object_ref (self);

}

/*
 * g_cclosure_marshal_BOOLEAN__VOID:
 *
 * A private marshaller for handlers which take no parameters and
 * return a boolean.
 */
static void
g_cclosure_marshal_BOOLEAN__VOID (GClosure     *closure,
                               GValue       *return_value G_GNUC_UNUSED,
                               guint         n_param_values,
                               const GValue *param_values,
                               gpointer      invocation_hint G_GNUC_UNUSED,
                               gpointer      marshal_data)
{
  typedef gboolean (*GMarshalFunc_BOOLEAN__VOID) (gpointer     data1,
					      gpointer     data2);
  register GMarshalFunc_BOOLEAN__VOID callback;
  register GCClosure *cc = (GCClosure*) closure;
  register gpointer data1, data2;
  gboolean v_return;

  g_return_if_fail (n_param_values == 1);

  if (G_CCLOSURE_SWAP_DATA (closure))
    {
      data1 = closure->data;
      data2 = g_value_peek_pointer (param_values + 0);
    }
  else
    {
      data1 = g_value_peek_pointer (param_values + 0);
      data2 = closure->data;
    }
  callback = (GMarshalFunc_BOOLEAN__VOID) (marshal_data ? marshal_data : cc->callback);

    v_return = callback (data1, data2);
    g_value_set_boolean (return_value, v_return);
}


/*
 * g_cclosure_marshal_BOOLEAN__STRING:
 *
 * A private marshaller for handlers which take a string parameter and
 * return a boolean.
 */
static void
g_cclosure_marshal_BOOLEAN__STRING (GClosure     *closure,
                               GValue       *return_value G_GNUC_UNUSED,
                               guint         n_param_values,
                               const GValue *param_values,
                               gpointer      invocation_hint G_GNUC_UNUSED,
                               gpointer      marshal_data)
{
  typedef gboolean (*GMarshalFunc_BOOLEAN__STRING) (gpointer     data1,
						    const char     *arg1,
						    gpointer     data2);
  register GMarshalFunc_BOOLEAN__STRING callback;
  register GCClosure *cc = (GCClosure*) closure;
  register gpointer data1, data2;
  gboolean v_return;

  g_return_if_fail (n_param_values == 1);

  if (G_CCLOSURE_SWAP_DATA (closure))
    {
      data1 = closure->data;
      data2 = g_value_peek_pointer (param_values + 0);
    }
  else
    {
      data1 = g_value_peek_pointer (param_values + 0);
      data2 = closure->data;
    }
  callback = (GMarshalFunc_BOOLEAN__STRING) (marshal_data ? marshal_data : cc->callback);

    v_return = callback (data1,            
			 g_value_get_string (param_values + 1),
			 data2);
    g_value_set_boolean (return_value, v_return);
}


/* 
 * block_termination_accumulator:
 *
 * A signal accumulator function for the NSApplicationShouldTerminate
 * signal.
 *
 * If a handler returns TRUE than we need to stop termination, so we
 * set the return value accumulator to TRUE and return FALSE (there's
 * no point in asking more handlers; we're going to abort the
 * shutdown). Otherwise, set the return value to FALSE (don't block
 * termination) and continue looking for handlers.
 *
 * Returns: gboolean
 */
static gboolean
block_termination_accumulator(GSignalInvocationHint *ihint, GValue *accum,
			const GValue *retval, gpointer data)
{
  if (g_value_get_boolean(retval)) {
    g_value_set_boolean(accum, TRUE);
    return FALSE; //Stop handling the signal
  }
  g_value_set_boolean(accum, FALSE);
  return TRUE; //Continue handling the signal
 }

/*
 * global_event_filter_func
 * @windowing_event: The event to process as a gpointer
 * @event: The corresponding GdkEvent
 * @user_data: Pointer registerd with the signal handler.
 *
 * Processes Cocoa KeyEquivalents which don't have Gtk or application
 * implementations. In general, these must be KeyEquivalents like
 * command-q which are provided by the Cocoa framework.
 *
 * Returns: Whether to continue event processing.
 */
static GdkFilterReturn
global_event_filter_func (gpointer  windowing_event, GdkEvent *event,
                          gpointer  user_data)
{
  NSEvent *nsevent = windowing_event;
  GtkOSXApplication* app = user_data;

  /* Handle menu events with no window, since they won't go through the
   * regular event processing.
   */
  if ([nsevent type] == NSKeyDown && 
      gtk_osxapplication_use_quartz_accelerators(app) )
    if ([[NSApp mainMenu] performKeyEquivalent: nsevent])
      return GDK_FILTER_TRANSLATE;
  return GDK_FILTER_CONTINUE;
}

struct construction_args {
  GType gtype;
  guint n_props;
  GObjectConstructParam *props;
};

enum {
    DidBecomeActive,
    WillResignActive,
    BlockTermination,
    OpenFile,
    LastSignal
};

static guint gtk_osxapplication_signals[LastSignal] = {0};
/*
 * gtk_osxapplication_init:
 * @self: The GtkOSXApplication object.
 *
 * Class initialization. Includes creating a bunch of special signals
 * for echoing Cocoa signals through to the application.
 */
static void
gtk_osxapplication_init (GtkOSXApplication *self)
{
  [NSApplication sharedApplication];
  self->priv = GTK_OSX_APPLICATION_GET_PRIVATE (self);
  self->priv->use_quartz_accelerators = TRUE;
  self->priv->dock_menu = NULL;
  gdk_window_add_filter (NULL, global_event_filter_func, (gpointer)self);



  /* this will stick around for ever ... is that OK ? */

  [ [GtkApplicationNotificationObject alloc] init];
  [ NSApp setDelegate: [GtkApplicationDelegate new]];
}

/**
 * gtk_osxapplication_class_init:
 * @klass: The class type pointer
 *
 * Not normaly called directly; Use g_object_new(GTK_TYPE_OSXAPPLICATION)
 */
void 
gtk_osxapplication_class_init(GtkOSXApplicationClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
  g_type_class_add_private(klass, sizeof(GtkOSXApplicationPrivate));
  gobject_class->constructor = gtk_osxapplication_constructor;
/**
 * GtkOSXApplication::NSApplicationDidBecomeActive:
 * @app: The application object
 * #user_data: Data appended at connection
 *
 * Emitted by the Application Delegate when the application receives
 * an NSApplicationDidBecomeActive notification. Connect a handler if
 * there is anything you need to do when the application is activated.
 */
gtk_osxapplication_signals[DidBecomeActive] =
    g_signal_new("NSApplicationDidBecomeActive",
	       GTK_TYPE_OSX_APPLICATION,
	       G_SIGNAL_NO_RECURSE | G_SIGNAL_ACTION,
	       0, NULL, NULL,
	       g_cclosure_marshal_VOID__VOID,
	       G_TYPE_NONE, 0);
/**
 * GtkOSXApplication::NSApplicationWillResignActive:
 * @app: The application object
 * @user_data: Data appended at connection
 *
 * This signal is emitted by the Application Delegate when the
 * application receives an NSApplicationWillResignActive
 * notification. Connect a handler to it if there's anything your
 * application needs to do to prepare for inactivity.
 */
gtk_osxapplication_signals[WillResignActive] =
    g_signal_new("NSApplicationWillResignActive",
	       GTK_TYPE_OSX_APPLICATION,
	       G_SIGNAL_NO_RECURSE | G_SIGNAL_ACTION,
	       0, NULL, NULL,
	       g_cclosure_marshal_VOID__VOID,
	       G_TYPE_NONE, 0);

/**
 * GtkOSXApplication::NSApplicationBlockTermination:
 * @app: The application object
 * @user_data: Data appended at connection
 *
 * Emitted by the Application Delegate when the application reeeives
 * an NSApplicationShouldTerminate notification. Perform any cleanup
 * you need to do (e.g., saving files) before exiting. Returning FALSE
 * will allow further handlers to run and if none return TRUE, the
 * application to shut down. Returning TRUE will veto shutdown and
 * stop emission, so later handlers will not run.
 *
 * Returns: Boolean indicating that further emission and application
 * termination should be blocked.
 */
gtk_osxapplication_signals[BlockTermination] =
    g_signal_new("NSApplicationBlockTermination",
	       GTK_TYPE_OSX_APPLICATION,
	       G_SIGNAL_NO_RECURSE | G_SIGNAL_ACTION,
	       0, block_termination_accumulator, NULL,
	       g_cclosure_marshal_BOOLEAN__VOID,
	       G_TYPE_BOOLEAN, 0);

/**
 * GtkOSXApplication::NSApplicationOpenFile:
 * @app: The application object
 * @path: A UTF8-encoded file path to open.
 * @user_data: Data attached at connection
 *
 * Emitted when a OpenFile, OpenFiles, or OpenEmptyFile event is
 * received from the operating system. This signal does not implement
 * drops, but it does implement "open with" events from Finder. An
 * OpenEmptyFile is received at launch in Python applications.
 *
 * Returns: Boolean indicating success at opening the file.
 */
gtk_osxapplication_signals[OpenFile] =
    g_signal_new("NSApplicationOpenFile",
	       GTK_TYPE_OSX_APPLICATION,
	       G_SIGNAL_NO_RECURSE | G_SIGNAL_ACTION,
	       0, NULL, NULL,
	       g_cclosure_marshal_BOOLEAN__STRING,
	       G_TYPE_BOOLEAN, 1, G_TYPE_STRING);

}

/**
 * gtk_osxapplication_ready:
 * @self: The GtkOSXApplication object
 *
 * Inform Cocoa that application initialization is complete. 
 */
void
gtk_osxapplication_ready (GtkOSXApplication *self)
{
  [ NSApp finishLaunching ];
}

/**
 * gtk_osxapplication_cleanup:
 * @self: The GtkApplication object
 *
 * Destroy the GtkOSXApplication object. Not normally needed, as the
 * object is expected to remain until the application quits.
 */
void
gtk_osxapplication_cleanup(GtkOSXApplication *self)
{
  //FIXME: release each window's menubar
  [self->priv->dock_menu release];
  
}

/*
 * window_focus_cb:
 * @window: The application window receiving focus
 * @event: The GdkEvent. Not used.
 * @menubar: The GNSMenubar associated with window
 *
 * Changes the active menubar when the application switches
 * windows. If you switch window focus programmatically, make sure
 * that the activate signal is emitted for the new window to trigger
 * this handler.
 */
static gboolean
window_focus_cb (GtkWindow* window, GdkEventFocus *event, GNSMenuBar *menubar)
{
  [NSApp setMainMenu: menubar];
  return FALSE;
}

/**
 * gtk_osxapplication_set_menu_bar:
 * @self: The GtkOSXApplication object
 * @menu_shell: The GtkMenuBar that you want to set.
 *
 * Set a window's menubar as the application menu bar. Call this once
 * for each window as you create them. It works best if the menubar is
 * reasonably fully populated before you call it. Once set, it will
 * stay syncronized through signals as long as you don't disconnect or
 * block them.
 */
void
gtk_osxapplication_set_menu_bar (GtkOSXApplication *self, GtkMenuShell *menu_shell)
{
  GNSMenuBar* cocoa_menubar;
  GtkWidget *parent = gtk_widget_get_toplevel(GTK_WIDGET(menu_shell));
  GdkWindow *win = gtk_widget_get_window(parent);
  NSWindow *nswin; 

  g_return_if_fail (GTK_IS_MENU_SHELL (menu_shell));
  g_return_if_fail (win != NULL);
  g_return_if_fail (GDK_IS_WINDOW(win));
  nswin = gdk_quartz_window_get_nswindow(win);
  g_return_if_fail(nswin != NULL);

  cocoa_menubar = (GNSMenuBar*)cocoa_menu_get(GTK_WIDGET (menu_shell));
  if (!cocoa_menubar) {
    cocoa_menubar = [[GNSMenuBar alloc] initWithGtkMenuBar: 
		     GTK_MENU_BAR(menu_shell)];
    cocoa_menu_connect(GTK_WIDGET (menu_shell), cocoa_menubar);
  /* turn off auto-enabling for the menu - its silly and slow and
     doesn't really make sense for a Gtk/Cocoa hybrid menu.
  */
    [cocoa_menubar setAutoenablesItems:NO];

  }
  if (cocoa_menubar != [NSApp mainMenu])
    [NSApp setMainMenu: cocoa_menubar];

  [cocoa_menubar setAppMenu: create_apple_menu (self)];

  emission_hook_id =
    g_signal_add_emission_hook (g_signal_lookup ("parent-set",
						 GTK_TYPE_WIDGET),
				0,
				parent_set_emission_hook,
				cocoa_menubar, NULL);


  g_signal_connect (menu_shell, "destroy",
		    G_CALLBACK (parent_set_emission_hook_remove),
		    NULL);

  g_signal_connect (parent, "focus-in-event", 
		    G_CALLBACK(window_focus_cb),
		    cocoa_menubar);

  cocoa_menu_item_add_submenu (menu_shell, cocoa_menubar, TRUE, FALSE);
  if (![cocoa_menubar itemWithTitle: @"Help"]) {
    [cocoa_menubar setHelpMenu: [[GNSMenuItem alloc] initWithTitle: @"Help"
				 action: NULL keyEquivalent: @""]];
    [cocoa_menubar addItem: [cocoa_menubar helpMenu]];
    [NSApp setHelpMenu: [cocoa_menubar helpMenu]];
  }
  if (![cocoa_menubar itemWithTitle: @"Window"]) 
    [cocoa_menubar setWindowMenu:  create_window_menu (self, nswin)];

}

/**
 * gtk_osxapplication_sync_menubar:
 * @self: The GtkOSXApplication object
 *
 * Syncornize the active window's GtkMenuBar with the OSX menu
 * bar. You should only need this if you have programmatically changed
 * the menus with signals blocked or disconnected.
 */
void
gtk_osxapplication_sync_menubar (GtkOSXApplication *self)
{
  [(GNSMenuBar*)[NSApp mainMenu] resync];
}


/**
 * gtk_osxapplication_add_app_menu_group:
 * @self: The GtkOSXApplication object
 *
 * GtkOSXApplicationMenuGroups are used to insert separators in the
 * App menu and to ensure that they are displayed only if not the
 * first item in the menu. Groups are added sequentially; there is no
 * provision for inserting them at a particular position.
 *
 * Returns: A new GtkOSXApplicationMenuGroup
 */
GtkOSXApplicationMenuGroup *
gtk_osxapplication_add_app_menu_group (GtkOSXApplication* self )
{
  GNSMenuBar *menubar = (GNSMenuBar*)[NSApp mainMenu];
  GtkOSXApplicationMenuGroup *group = [menubar addGroup];
    return group;
}

/**
 * gtk_osxapplication_add_app_menu_item:
 * @self: The GtkOSXApplication object
 * @group: The GtkOSXApplicationMenuGroup to which the menu item should be 
 * added.
 * @menu_item: The GtkMenuItem to add to the group.
 *
 * Certain menu items (About, Check for updates, and Preferences in
 * particular) are normally found in the so-called Application menu
 * (the first one on the menubar, named after the application) in OSX
 * applications. This function will create a menu entry for such a
 * menu item, removing it from its original menu in the Gtk
 * application.
 *
 * Use this after calling gtk_osxapplication_set_menu_bar(), first
 * creating a GtkOSXApplicationMenuGroup to hold it with
 * gtk_osxapplication_add_menu_group().
 *
 * Don't use it for Quit! A Quit menu item is created automatically
 * along with the Application menu. Just hide your Gtk Quit menu item.
 */
void
gtk_osxapplication_add_app_menu_item (GtkOSXApplication *self,
				   GtkOSXApplicationMenuGroup *group,
				   GtkMenuItem *menu_item)
{
  // we know that the application menu is always the submenu of the first item in the main menu
  GNSMenuBar *menubar = (GNSMenuBar*)[NSApp mainMenu];
  GList   *list = NULL, *menu_groups = [menubar app_menu_groups];
  gint     index = 0;
  NSMenu *app_menu = [[menubar itemAtIndex: 0] submenu];
  GtkWidget *parent = gtk_widget_get_toplevel (GTK_WIDGET (menu_item));

  g_return_if_fail (group != NULL);
  g_return_if_fail (GTK_IS_MENU_ITEM (menu_item));
  g_return_if_fail(parent != NULL);

  for (list = menu_groups; list; list = g_list_next (list))
    {
      GtkOSXApplicationMenuGroup *list_group = (GtkOSXApplicationMenuGroup*) list->data;

      index += g_list_length (list_group->items);

      /*  adjust index for the separator between groups, but not
       *  before the first group
       */
      if (list_group->items && list->prev)
	index++;

      if (group == list_group)
	{
	  /*  add a separator before adding the first item, but not
	   *  for the first group
	   */
		
	  if (!group->items && list->prev)
	    {
	      [app_menu insertItem:[NSMenuItem separatorItem] 
	       atIndex:index+1];
	      index++;
	    }
	  DEBUG ("Add to APP menu bar %s\n", get_menu_label_text (GTK_WIDGET(menu_item), NULL));
	  cocoa_menu_item_add_item ([[[NSApp mainMenu] itemAtIndex: 0] submenu],
				    GTK_WIDGET(menu_item), index + 1);

	  group->items = g_list_append (group->items, menu_item);
	  return;
	}
    }

  if (!list)
    g_warning ("%s: app menu group %p does not exist",
	       G_STRFUNC, group);
}

/* Dock support */
/* A bogus prototype to shut up a compiler warning. This function is for GtkApplicationDelegate and is not public. */
NSMenu* _gtk_osxapplication_dock_menu(GtkOSXApplication *self);

/**
 * _gtk_osxapplication_dock_menu:
 * @self: The GtkOSXApplication object.
 *
 * Return the dock menu to the Application Delegate; if not null, it
 * will be added to the dock menu.
 *
 * Returns: NSMenu*
 */
NSMenu*
_gtk_osxapplication_dock_menu(GtkOSXApplication *self)
{
  return(self->priv->dock_menu);
}

/**
 * gtk_osxapplication_set_dock_menu:
 * @self: The GtkOSXApplication object
 * @menu_shell: A GtkMenu (cast it with GTK_MENU_SHELL() when you
 * pass it in
 *
 * Set a GtkMenu as the dock menu.  
 *
 * This menu does not have a "sync" function, so changes made while
 * signals are disconnected will not update the menu which appears in
 * the dock, and may produce strange results or crashes if a
 * GtkMenuItem or GtkAction associated with a dock menu item is
 * deallocated.
 */
void
gtk_osxapplication_set_dock_menu(GtkOSXApplication *self,
			      GtkMenuShell *menu_shell)
{
  g_return_if_fail (GTK_IS_MENU_SHELL (menu_shell));
  if (!self->priv->dock_menu) {
    self->priv->dock_menu = [[NSMenu alloc] initWithTitle: @""]; 
    cocoa_menu_item_add_submenu(menu_shell, self->priv->dock_menu, FALSE, FALSE);
    [self->priv->dock_menu retain];
  }
}

/*
 * nsimage_from_resource:
 * @name: The filename
 * @type: The extension (e.g., jpg) of the filename
 * @subdir: The subdirectory of $Bundle/Contents/Resources in which to
 * look for the file.
 *
 * Retrieve an image file from the bundle and return an NSImage* of it.
 *
 * Returns: An autoreleased NSImage
 */
static NSImage*
nsimage_from_resource(const gchar *name, const gchar* type, const gchar* subdir)
{
  NSString *ns_name, *ns_type, *ns_subdir, *path;
  NSImage *image;
  g_return_val_if_fail(name != NULL, NULL);
  g_return_val_if_fail(type != NULL, NULL);
  g_return_val_if_fail(subdir != NULL, NULL);
  ns_name = [NSString stringWithUTF8String: name];
  ns_type = [NSString stringWithUTF8String: type];
  ns_subdir = [NSString stringWithUTF8String: subdir];
  path = [[NSApp mainBundle] pathForResource: ns_name
		     ofType: ns_type inDirectory: ns_subdir];
  if (!path) {
    [ns_name release];
    [ns_type release];
    [ns_subdir release];
    return NULL;
  }
  image = [[[NSImage alloc] initWithContentsOfFile: path] autorelease];
  [ns_name release];
  [ns_type release];
  [ns_subdir release];
  [path release];
  return image;
}

/*
 * nsimage_from_pixbuf:
 * @pixbuf: The GdkPixbuf* to convert
 *
 * Create an NSImage from a CGImageRef.
 * Lifted from http://www.cocoadev.com/index.pl?CGImageRef
 *
 * Returns: An auto-released NSImage*
 */
static NSImage*
nsimage_from_pixbuf(GdkPixbuf *pixbuf)
{
  CGImageRef image = NULL;
  NSRect imageRect = NSMakeRect(0.0, 0.0, 0.0, 0.0);
  CGContextRef imageContext = nil;
  NSImage* newImage = nil;

  g_return_val_if_fail (pixbuf !=  NULL, NULL);
  image = ige_mac_image_from_pixbuf (pixbuf);
  // Get the image dimensions.
  imageRect.size.height = CGImageGetHeight(image);
  imageRect.size.width = CGImageGetWidth(image);

  // Create a new image to receive the Quartz image data.
  newImage = [[[NSImage alloc] initWithSize:imageRect.size] autorelease];
  [newImage lockFocus];

  // Get the Quartz context and draw.
  imageContext = (CGContextRef)[[NSGraphicsContext currentContext]
				graphicsPort];
  CGContextDrawImage(imageContext, *(CGRect*)&imageRect, image);
  [newImage unlockFocus];
  CGImageRelease (image);
  return newImage;
}

/** Set the dock icon from a GdkPixbuf
 * @param self The GtkOSXApplication
 * @param pixbuf The pixbuf. Pass NULL to reset the icon to its default.
 */
void
gtk_osxapplication_set_dock_icon_pixbuf(GtkOSXApplication *self,
					  GdkPixbuf *pixbuf)
{
  if (!pixbuf)
    [NSApp setApplicationIconImage: nil];
  else
    [NSApp setApplicationIconImage: nsimage_from_pixbuf(pixbuf)];

}

/**
 * gtk_osxapplication_set_dock_icon_resource:
 * @self: The GtkOSXApplication
 * @name: The ame of the image file
 * @type: The extension (e.g., jpg) of the filename
 * @subdir: The subdirectory of $Bundle/Contents/Resources in which to
 * look for the file.
 *
 * Set the dock icon from an image file in the bundle/
 */
void
gtk_osxapplication_set_dock_icon_resource(GtkOSXApplication *self,
					    const gchar  *name,
					    const gchar  *type,
					    const gchar  *subdir)
{
  NSImage *image = nsimage_from_resource(name, type, subdir);
  [NSApp setApplicationIconImage: image];
  [image release];
}

/**
 * gtk_osxapplication_attention_request:
 * @self: The GtkOSXApplication pointer
 * @type: CRITICAL_REQUEST or INFO_REQUEST
 *
 * Create an attention request.  If type is CRITICAL_REQUEST, the
 * dock icon will bounce until cancelled the application receives
 * focus; otherwise it will bounce for 1 second -- but the attention
 * request will remain asserted until cancelled or the application
 * receives focus. This function has no effect if the application has focus.
 *
 * Returns: A the attention request ID. Pass this id to
 * gtk_osxapplication_cancel_attention_request.
 */
gint
gtk_osxapplication_attention_request(GtkOSXApplication *self,
				  GtkOSXApplicationAttentionType type)
{
  return (gint)[NSApp requestUserAttention: (NSRequestUserAttentionType)type];
}

/**
 * gtk_osxapplication_cancel_attention_request:
 * @self: The application
 * @id: The integer attention request id returned from
 * gtk_osxapplication_attention_request.
 *
 * Cancel an attention request created with
 * gtk_osxapplication_attention_request.
 */
void
gtk_osxapplication_cancel_attention_request(GtkOSXApplication *self, gint id)
{
  [NSApp cancelUserAttentionRequest: (NSInteger)id];
}

/**
 * gtk_osxapplication_get_bundle_path:
 * @self: The GtkOSXApplication. Not Used.
 *
 * Return the root path of the bundle or the directory containing the
 *  executable if it isn't actually a bundle.
 *
 * Returnl path The bundle's absolute path
 */
const gchar*
gtk_osxapplication_get_bundle_path(GtkOSXApplication *self)
{
  return [[[NSBundle mainBundle] bundlePath] UTF8String];
}

/**
 * gtk_osxapplication_get_bundle_id:
 * @self: The GtkOSXApplication. Not Used.
 *
 *Return the value of the CFBundleIdentifier key from the bundle's Info.plist
 *
 * This will return NULL if it's not really a bundle, there's no
 * Info.plist, or if Info.plist doesn't have a CFBundleIdentifier key
 * (So if you need to detect being in a bundle, make sure that your
 * bundle has that key!)
 *
 * Returns: The string value of CFBundleIdentifier, or NULL if there is none.
 */
const gchar*
gtk_osxapplication_get_bundle_id(GtkOSXApplication *self)
{
  return [[[NSBundle mainBundle] bundleIdentifier] UTF8String];
}

/**
 * gtk_osxapplication_get_resource_path:
 * @self: The GtkOSXApplication. Not Used.
 *
 * Return the Resource path for the bundle or the directory containing the
 *  executable if it isn't actually a bundle.
 *
 * @return path The absolute resource path
 */
const gchar*
gtk_osxapplication_get_resource_path(GtkOSXApplication *self)
{
  return [[[NSBundle mainBundle] resourcePath] UTF8String];
}


/**
 * gtk_osxapplication_get_executable_path:
 * @self: The GtkOSXApplication. Not Used.
 *
 * Return the executable path, including file name
 *
 * Returns: The path to the primary executable
 */
const gchar*
gtk_osxapplication_get_executable_path(GtkOSXApplication *self)
{
  return [[[NSBundle mainBundle] executablePath] UTF8String];
}

/**
 * gtk_osxapplication_get_bundle_info:
 * @self: The GtkOSXApplication. Not Used.
 * @key: The key, as a normal UTF8 string.
 *
 * Return the NSObject pointed to by the provided key.
 *
 * Be careful with this! It returns a gpointer to an NSObject, so
 * you'll need to check the object class before doing anything, and
 * then cast it appropriately. Don't try this if you don't know Cocoa
 * programming!
 *
 * @return A pointer to the NSObject stored with that key.
 */
gpointer
gtk_osxapplication_get_bundle_info(GtkOSXApplication *self, const gchar *key)
{
  return (gpointer)[[NSBundle mainBundle] objectForInfoDictionaryKey:
			  [NSString stringWithUTF8String: key]];
}