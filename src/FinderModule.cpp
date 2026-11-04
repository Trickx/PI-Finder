#include <pcl/Bitmap.h>
#include <pcl/CheckBox.h>
#include <pcl/Dialog.h>
#include <pcl/Edit.h>
#include <pcl/Console.h>
#include <pcl/ErrorHandler.h>
#include <pcl/File.h>
#include <pcl/GlobalSettings.h>
#include <pcl/KeyCodes.h>
#include <pcl/MessageBox.h>
#include <pcl/MetaModule.h>
#include <pcl/MetaProcess.h>
#include <pcl/Process.h>
#include <pcl/ProcessImplementation.h>
#include <pcl/ProcessInterface.h>
#include <pcl/PushButton.h>
#include <pcl/Settings.h>
#include <pcl/Sizer.h>
#include <pcl/Timer.h>
#include <pcl/TreeBox.h>

#if defined( __PCL_MACOSX )
#  include <CoreGraphics/CoreGraphics.h>
#elif defined( __PCL_WINDOWS )
#  define NOMINMAX
#  include <windows.h>
#else
#  include <X11/Xlib.h>
#endif

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <memory>
#include <vector>

#define MODULE_VERSION_MAJOR     1
#define MODULE_VERSION_MINOR     0
#define MODULE_VERSION_REVISION  3
#define MODULE_VERSION_BUILD     0
#define MODULE_VERSION_LANGUAGE  eng

using namespace pcl;

namespace
{

void RestoreVisiblePosition( Control& window, const pcl::Point* savedPosition )
{
   auto isVisibleOnDisplay = []( const pcl::Rect& windowRect, const pcl::Rect& displayRect )
   {
      int visibleWidth = Min( windowRect.x1, displayRect.x1 ) - Max( windowRect.x0, displayRect.x0 );
      int visibleHeight = Min( windowRect.y1, displayRect.y1 ) - Max( windowRect.y0, displayRect.y0 );
      return visibleWidth >= Min( 64, windowRect.Width() ) &&
             visibleHeight >= Min( 32, windowRect.Height() );
   };

   auto moveToDisplayCenter = [&window]( const pcl::Rect& displayRect )
   {
      window.Move( displayRect.x0 + (displayRect.Width() - window.FrameWidth())/2,
                   displayRect.y0 + (displayRect.Height() - window.FrameHeight())/2 );
   };

   pcl::Rect windowRect = window.FrameRect();
   if ( savedPosition != nullptr )
   {
      windowRect.x1 = savedPosition->x + windowRect.Width();
      windowRect.y1 = savedPosition->y + windowRect.Height();
      windowRect.x0 = savedPosition->x;
      windowRect.y0 = savedPosition->y;
   }

#if defined( __PCL_MACOSX )
   uint32_t displayCount = 0;
   if ( CGGetActiveDisplayList( 0, nullptr, &displayCount ) != kCGErrorSuccess || displayCount == 0 )
      return;

   std::vector<CGDirectDisplayID> displays( displayCount );
   if ( CGGetActiveDisplayList( displayCount, displays.data(), &displayCount ) != kCGErrorSuccess )
      return;

   pcl::Rect primaryDisplay;
   std::vector<pcl::Rect> displayRects;
   for ( uint32_t i = 0; i < displayCount; ++i )
   {
      CGRect bounds = CGDisplayBounds( displays[i] );
      pcl::Rect displayRect( int( bounds.origin.x ), int( bounds.origin.y ),
                             int( bounds.origin.x + bounds.size.width ),
                             int( bounds.origin.y + bounds.size.height ) );
      if ( i == 0 )
         primaryDisplay = displayRect;
      if ( displays[i] == CGMainDisplayID() )
         primaryDisplay = displayRect;
      displayRects.push_back( displayRect );
   }

   for ( const pcl::Rect& displayRect : displayRects )
      if ( isVisibleOnDisplay( windowRect, displayRect ) )
      {
         if ( savedPosition != nullptr )
            window.Move( *savedPosition );
         return;
      }
   if ( displayRects.empty() )
      return;
   moveToDisplayCenter( primaryDisplay );
#elif defined( __PCL_WINDOWS )
   struct DisplayRectangles
   {
      std::vector<pcl::Rect> displays;
      pcl::Rect primary;
      bool hasPrimary = false;
   } displayRects;
   EnumDisplayMonitors( nullptr, nullptr,
      []( HMONITOR monitor, HDC, LPRECT bounds, LPARAM data ) -> BOOL
      {
         auto& rectangles = *reinterpret_cast<DisplayRectangles*>( data );
         pcl::Rect displayRect( bounds->left, bounds->top, bounds->right, bounds->bottom );
         rectangles.displays.push_back( displayRect );
         MONITORINFO monitorInfo = { sizeof( MONITORINFO ) };
         if ( GetMonitorInfo( monitor, &monitorInfo ) && (monitorInfo.dwFlags & MONITORINFOF_PRIMARY) != 0 )
         {
            rectangles.primary = displayRect;
            rectangles.hasPrimary = true;
         }
         if ( rectangles.displays.size() == 1 && !rectangles.hasPrimary )
            rectangles.primary = displayRect;
         return TRUE;
      }, reinterpret_cast<LPARAM>( &displayRects ) );

   for ( const pcl::Rect& displayRect : displayRects.displays )
      if ( isVisibleOnDisplay( windowRect, displayRect ) )
      {
         if ( savedPosition != nullptr )
            window.Move( *savedPosition );
         return;
      }
   if ( !displayRects.displays.empty() )
      moveToDisplayCenter( displayRects.primary );
#else
   Display* display = XOpenDisplay( nullptr );
   if ( display == nullptr )
      return;

   int screen = DefaultScreen( display );
   pcl::Rect displayRect( 0, 0, XDisplayWidth( display, screen ), XDisplayHeight( display, screen ) );
   XCloseDisplay( display );
   if ( isVisibleOnDisplay( windowRect, displayRect ) )
   {
      if ( savedPosition != nullptr )
         window.Move( *savedPosition );
   }
   else
      moveToDisplayCenter( displayRect );
#endif
}

const char* const SearchIconSVG =
   "<svg xmlns='http://www.w3.org/2000/svg' width='32' height='32' viewBox='0 0 32 32'>"
   "<circle cx='13' cy='13' r='9' fill='none' stroke='#58a6ff' stroke-width='3'/>"
   "<path d='m20 20 8 8' stroke='#58a6ff' stroke-width='3' stroke-linecap='round'/>"
   "</svg>";

enum class EntryType
{
   Process,
   Script
};

// Entries are heap-allocated and never copied or moved: pcl::Bitmap handles
// obtained from the core must not be duplicated and released again, otherwise
// the core throws from ~UIObject() and PixInsight terminates.
struct SearchEntry
{
   EntryType type;
   IsoString processId;
   String scriptPath;
   String iconPath;
   String label;
   Bitmap icon;
};

class FinderModule : public MetaModule
{
public:
   IsoString Name() const override
   {
      return "Finder";
   }

   const char* Version() const override
   {
      return PCL_MODULE_VERSION( MODULE_VERSION_MAJOR,
                                 MODULE_VERSION_MINOR,
                                 MODULE_VERSION_REVISION,
                                 MODULE_VERSION_BUILD,
                                 MODULE_VERSION_LANGUAGE );
   }

   void OnLoad() override;
};

class FinderInterface;

class FinderInstance : public ProcessImplementation
{
public:
   FinderInstance( const MetaProcess* m ) : ProcessImplementation( m )
   {
   }

   FinderInstance( const FinderInstance& x ) : ProcessImplementation( x )
   {
   }

   void Assign( const ProcessImplementation& ) override
   {
   }

   bool CanExecuteOn( const View&, String& whyNot ) const override
   {
      whyNot = "Finder is a user interface and cannot be applied to an image.";
      return false;
   }

   bool CanExecuteGlobal( String& ) const override
   {
      return true;
   }

   // Global execution opens the search window.
   bool ExecuteGlobal() override;

   bool IsHistoryUpdater( const View& ) const override
   {
      return false;
   }
};

class FinderProcess : public MetaProcess
{
public:
   IsoString Id() const override
   {
      return "Finder";
   }

   IsoString Categories() const override
   {
      return "Tricx";
   }

   String Description() const override
   {
      return "Search and launch installed processes and PJSR scripts.";
   }

   IsoString IconImageSVG() const override
   {
      return SearchIconSVG;
   }

   ProcessInterface* DefaultInterface() const override;

   bool CanProcessViews() const override
   {
      return false;
   }

   // The PixInsight core rejects processes without any execution capability.
   bool CanProcessGlobal() const override
   {
      return true;
   }

   ProcessImplementation* Create() const override
   {
      return new FinderInstance( this );
   }

   ProcessImplementation* Clone( const ProcessImplementation& p ) const override
   {
      return new FinderInstance( static_cast<const FinderInstance&>( p ) );
   }
};

FinderProcess* TheFinderProcess = nullptr;
FinderInterface* TheFinderInterface = nullptr;

void AddUniquePath( std::vector<String>& paths, const String& path )
{
   if ( !path.IsEmpty() && File::DirectoryExists( path ) &&
        std::find( paths.begin(), paths.end(), path ) == paths.end() )
      paths.push_back( File::FullPath( path ) );
}

std::vector<String> ScriptSearchPaths()
{
   std::vector<String> paths;
   // The standard script folder of the running installation, on every platform.
   AddUniquePath( paths, PixInsightSettings::GlobalString( "Application/SrcDirectory" ) + "/scripts" );

   String home = File::HomeDirectory();
   AddUniquePath( paths, home + "/PixInsight/Scripts" );
   AddUniquePath( paths, home + "/Documents/PixInsight/Scripts" );

   // Additional roots can be provided as a list, separated as PATH is: by
   // semicolons on Windows (drive letters contain colons), else by colons.
#ifdef __PCL_WINDOWS
   const char listSeparator = ';';
#else
   const char listSeparator = ':';
#endif
   if ( const char* extraPaths = std::getenv( "PIXINSIGHT_SCRIPT_PATHS" ) )
   {
      String list( extraPaths );
      while ( !list.IsEmpty() )
      {
         String path;
         size_type separator = list.FindFirst( listSeparator );
         if ( separator == String::notFound )
         {
            path = list;
            list.Clear();
         }
         else
         {
            path = list.Substring( 0, separator );
            list = list.Substring( separator + 1 );
         }
         AddUniquePath( paths, path.Trimmed() );
      }
   }
   return paths;
}

String FindFileRecursive( const String& directory, const String& fileName )
{
   if ( File::Exists( directory + '/' + fileName ) )
      return directory + '/' + fileName;

   FindFileInfo info;
   for ( File::Find search( directory + "/*" ); search.NextItem( info ); )
      if ( info.IsDirectory() && info.name != "." && info.name != ".." && !info.IsSymbolicLink() )
      {
         String path = FindFileRecursive( directory + '/' + info.name, fileName );
         if ( !path.IsEmpty() )
            return path;
      }
   return String();
}

// Whether a script declares a #feature-id directive. PixInsight lists only
// such scripts as features; other .js files are mostly modules included by a
// main script and fail when run on their own.
bool IsFeatureScript( const IsoStringList& lines )
{
   for ( const IsoString& line : lines )
      if ( line.Trimmed().StartsWith( "#feature-id" ) )
         return true;
   return false;
}

// Resolves the #feature-icon directive of a script. Returns an empty string
// if the script declares no icon or the icon file does not exist.
String ScriptIconPath( const String& scriptPath, const IsoStringList& lines )
{
   IsoString iconSpec;
   for ( const IsoString& line : lines )
   {
      IsoString directive = line.Trimmed();
      if ( directive.StartsWith( "#feature-icon" ) )
      {
         iconSpec = directive.Substring( 13 ).Trimmed();
         if ( !iconSpec.IsEmpty() )
            break;
      }
   }
   if ( iconSpec.IsEmpty() )
      return String();

   String spec = iconSpec.UTF8ToUTF16();
   const String scriptIconsPrefix = "@script_icons_dir/";
   if ( spec.StartsWith( scriptIconsPrefix ) )
   {
      // Standard script icons live in <rsc>/icons/script, often inside a
      // per-script subdirectory that the directive does not name.
      String iconsDir = PixInsightSettings::GlobalString( "Application/RscDirectory" ) + "/icons/script";
      if ( !File::DirectoryExists( iconsDir ) )
         return String();
      String relativePath = spec.Substring( scriptIconsPrefix.Length() );
      if ( File::Exists( iconsDir + '/' + relativePath ) )
         return iconsDir + '/' + relativePath;
      return FindFileRecursive( iconsDir, File::ExtractNameAndExtension( relativePath ) );
   }

   String path = spec.StartsWith( '/' ) ? spec : File::ExtractDrive( scriptPath ) + File::ExtractDirectory( scriptPath ) + '/' + spec;
   return File::Exists( path ) ? path : String();
}

void EnumerateScripts( const String& directory, std::vector<std::unique_ptr<SearchEntry>>& entries )
{
   StringList subdirectories;
   FindFileInfo info;
   for ( File::Find search( directory + "/*" ); search.NextItem( info ); )
   {
      if ( info.name == "." || info.name == ".." || info.IsSymbolicLink() )
         continue;

      String path = directory + '/' + info.name;
      if ( info.IsDirectory() )
      {
         subdirectories.Add( path );
         continue;
      }

      String extension = File::ExtractExtension( info.name );
      bool isJavaScript = extension.FindFirstIC( ".js" ) == 0 && extension.Length() == 3;
      if ( isJavaScript || (extension.FindFirstIC( ".scp" ) == 0 && extension.Length() == 4) )
      {
         String scriptPath = File::FullPath( path );
         bool alreadyIndexed = std::any_of( entries.begin(), entries.end(),
            [&scriptPath]( const std::unique_ptr<SearchEntry>& entry )
            {
               return entry->type == EntryType::Script && entry->scriptPath == scriptPath;
            } );
         if ( alreadyIndexed )
            continue;

         IsoStringList lines;
         try
         {
            File::ReadTextFile( scriptPath ).Break( lines, '\n' );
         }
         catch ( ... )
         {
         }
         if ( isJavaScript && !IsFeatureScript( lines ) )
            continue;

         auto entry = std::make_unique<SearchEntry>();
         entry->type = EntryType::Script;
         entry->scriptPath = scriptPath;
         try
         {
            entry->iconPath = ScriptIconPath( scriptPath, lines );
         }
         catch ( ... )
         {
         }
         entry->label = File::ExtractName( info.name );
         entries.push_back( std::move( entry ) );
      }
   }

   for ( const String& subdirectory : subdirectories )
      EnumerateScripts( subdirectory, entries );
}

class FinderInterface : public ProcessInterface
{
public:
   IsoString Id() const override
   {
      return "Finder";
   }

   MetaProcess* Process() const override
   {
      return TheFinderProcess;
   }

   IsoString IconImageSVG() const override
   {
      return SearchIconSVG;
   }

   // Drag object (blue triangle): an instance opens the search window when
   // executed, e.g. from a process icon. The options live in the preferences
   // dialog, so the window itself only holds the search line and results.
   // Browse Documentation opens doc/tools/Finder/Finder.html.
   InterfaceFeatures Features() const override
   {
      return InterfaceFeature::DragObject | InterfaceFeature::BrowseDocumentationButton | InterfaceFeature::PreferencesButton;
   }

   void EditPreferences() override
   {
      PreferencesDialog dialog;
      dialog.Execute();
   }

   ProcessImplementation* NewProcess() const override
   {
      return new FinderInstance( TheFinderProcess );
   }

   // Instances have no parameters; importing one (e.g. by double-clicking a
   // process icon) just opens the window instead of triggering a core error.
   bool CanImportInstances() const override
   {
      return true;
   }

   bool ImportProcess( const ProcessImplementation& ) override
   {
      return true;
   }

   bool Launch( const MetaProcess&, const ProcessImplementation* instance, bool& dynamic, unsigned& ) override
   {
      dynamic = false;
      // Opened from a process icon (blue triangle): close again after launching.
      m_launchedFromInstance = instance != nullptr || m_executingInstance;
      try
      {
         if ( m_gui == nullptr )
         {
            InitializeControls();
            m_visibilityCheckPending = true;
         }

         // Loaded once per session so that icon bitmaps are never released.
         if ( m_entries.empty() )
         {
            LoadEntries();
            RefreshResults();
         }

         Show();
         Expand();
         m_gui->search.SelectAll();
         m_gui->search.Focus();
         return true;
      }
      ERROR_HANDLER
      return false;
   }

   // Called when a Finder instance is executed.
   bool LaunchFromInstance()
   {
      m_executingInstance = true;
      bool ok = ProcessInterface::Launch();
      m_executingInstance = false;
      return ok;
   }

   // Only called upon module unload, when the window may already be hidden or
   // gone. The geometry is therefore saved whenever it changes; this is just a
   // last update while the window is still in a valid state.
   void SaveSettings() const override
   {
      if ( m_gui != nullptr && m_savePositionEnabled && IsVisible() )
      {
         SaveWindowPosition( Position() );
         SaveWindowWidth();
      }
   }

   static bool MinimizeWithoutFocus()
   {
      bool minimize = true;
      Settings::Read( MinimizeKey(), minimize );
      return minimize;
   }

   static bool CloseAfterLaunch()
   {
      bool closeAfterLaunch = false;
      Settings::Read( CloseAfterLaunchKey(), closeAfterLaunch );
      return closeAfterLaunch;
   }

   static bool OpenAtStartup()
   {
      bool openAtStartup = false;
      Settings::Read( OpenAtStartupKey(), openAtStartup );
      return openAtStartup;
   }

   // The core is still starting up when modules are loaded, so the window is
   // opened from a timer once the event loop is running.
   void ScheduleStartupLaunch()
   {
      if ( !OpenAtStartup() )
         return;
      m_startupTimer = std::make_unique<Timer>();
      m_startupTimer->SetSingleShot();
      m_startupTimer->SetInterval( 1.0 );
      m_startupTimer->OnTimer( (Timer::timer_event_handler)&FinderInterface::e_StartupTimer, *this );
      m_startupTimer->Start();
   }

private:
   class PreferencesDialog : public Dialog
   {
   public:
      PreferencesDialog()
      {
         openAtStartup.SetText( "Open at startup" );
         openAtStartup.SetToolTip( "Open Finder automatically when PixInsight starts." );
         openAtStartup.SetChecked( OpenAtStartup() );

         closeAfterLaunch.SetText( "Close after launch" );
         closeAfterLaunch.SetToolTip( "Close Finder after launching a process or script with the Return key. "
                                      "Otherwise the window stays open as a narrow search line." );
         closeAfterLaunch.SetChecked( CloseAfterLaunch() );

         minimize.SetText( "Minimize" );
         minimize.SetToolTip( "Shrink Finder to a narrow search line while it does not have the focus." );
         minimize.SetChecked( MinimizeWithoutFocus() );

         ok.SetText( "OK" );
         ok.SetDefault();
         ok.OnClick( (Button::click_event_handler)&PreferencesDialog::e_Click, *this );
         cancel.SetText( "Cancel" );
         cancel.OnClick( (Button::click_event_handler)&PreferencesDialog::e_Click, *this );

         buttons.AddStretch();
         buttons.Add( ok );
         buttons.Add( cancel );
         buttons.SetSpacing( 8 );

         sizer.SetMargin( 8 );
         sizer.SetSpacing( 6 );
         sizer.Add( openAtStartup );
         sizer.Add( closeAfterLaunch );
         sizer.Add( minimize );
         sizer.AddSpacing( 8 );
         sizer.Add( buttons );
         SetSizer( sizer );

         SetWindowTitle( "Finder Preferences" );
         AdjustToContents();
         SetFixedSize();
      }

   private:
      CheckBox openAtStartup;
      CheckBox closeAfterLaunch;
      CheckBox minimize;
      PushButton ok;
      PushButton cancel;
      HorizontalSizer buttons;
      VerticalSizer sizer;

      void e_Click( Button& sender, bool )
      {
         if ( sender == ok )
         {
            Settings::Write( OpenAtStartupKey(), openAtStartup.IsChecked() );
            Settings::Write( CloseAfterLaunchKey(), closeAfterLaunch.IsChecked() );
            Settings::Write( MinimizeKey(), minimize.IsChecked() );
            Ok();
         }
         else
            Cancel();
      }
   };

   struct GUIData
   {
      Edit search;
      TreeBox results;
      VerticalSizer sizer;

      explicit GUIData( FinderInterface& parent )
         : search( String(), parent )
         , results( parent )
      {
      }
   };

   std::unique_ptr<GUIData> m_gui;
   std::unique_ptr<Timer> m_startupTimer;
   std::unique_ptr<Timer> m_focusTimer;
   std::unique_ptr<Timer> m_launchTimer;
   bool m_launchPending = false;
   bool m_pendingClose = false;
   EntryType m_pendingType = EntryType::Process;
   IsoString m_pendingProcessId;
   String m_pendingScriptPath;
   bool m_windowActive = true;
   bool m_launchedFromInstance = false;
   bool m_executingInstance = false;
   bool m_visibilityCheckPending = false;
   bool m_savePositionEnabled = false;
   bool m_collapsed = false;
   int m_expandedWidth = 0;
   int m_candidateWidth = 0;
   bool m_resultsHeightMeasured = false;
   bool m_resultsShown = true;

   static constexpr int VisibleResultRows = 3;
   static constexpr int ExpandedSearchWidth = 100;
   static constexpr int CollapsedSearchWidth = 100;

   void SetResultsHeight( int height )
   {
      m_gui->results.SetFixedHeight( height );
      UpdateWindowHeight();
   }

   // The window height follows the result list; only its width is resizable.
   // AdjustToContents() also sets the width to its natural size, so the
   // previous width is restored exactly (the layout's minimum still applies).
   void UpdateWindowHeight()
   {
      if ( m_collapsed )
         return;
      int width = Width();
      SetVariableHeight();
      AdjustToContents();
      SetFixedHeight();
      Resize( width, Height() );
   }

   // Without focus the window shrinks to a narrow search line.
   void Collapse()
   {
      if ( m_collapsed )
         return;
      m_collapsed = true;
      // The expanded width is tracked by TrackExpandedWidth(); Width() may
      // still be stale here, e.g. right after the width was restored.
      if ( m_expandedWidth <= 0 )
         m_expandedWidth = Width();
      m_gui->results.Hide();
      m_gui->search.SetScaledMinWidth( CollapsedSearchWidth );
      SetVariableSize();
      AdjustToContents();
      SetFixedSize();
   }

   void Expand()
   {
      if ( !m_collapsed )
         return;
      m_collapsed = false;
      m_gui->search.SetScaledMinWidth( ExpandedSearchWidth );
      m_gui->results.SetVisible( m_resultsShown );
      SetVariableSize();
      AdjustToContents();
      SetFixedHeight();
      if ( m_expandedWidth > 0 )
         Resize( m_expandedWidth, Height() );
      m_gui->search.Focus();
   }

   void ClearSearch()
   {
      m_gui->search.Clear();
      m_gui->search.Focus();
      RefreshResults();
   }

   static IsoString MinimizeKey()
   {
      return "Interfaces/Finder/Minimize";
   }

   static IsoString CloseAfterLaunchKey()
   {
      return "Interfaces/Finder/CloseAfterLaunch";
   }

   static IsoString OpenAtStartupKey()
   {
      return "Interfaces/Finder/OpenAtStartup";
   }

   void SaveWindowPosition( const pcl::Point& position ) const
   {
      IsoString key = SettingsKey() + "Geometry/";
      Settings::Write( key + "Left", position.x );
      Settings::Write( key + "Top", position.y );
   }

   // The height follows the result list, so only the (expanded) width is
   // stored, in logical pixels.
   static IsoString WidthKey()
   {
      return "Interfaces/Finder/Geometry/ExpandedWidth";
   }

   static constexpr int DefaultWidth = ExpandedSearchWidth + 2*8;

   void SaveWindowWidth() const
   {
      if ( m_expandedWidth > 0 )
         Settings::Write( WidthKey(), PhysicalPixelsToLogical( m_expandedWidth ) );
   }

   void RestoreWindowWidth()
   {
      int width = DefaultWidth;
      Settings::Read( WidthKey(), width );
      m_expandedWidth = LogicalPixelsToPhysical( Max( width, DefaultWidth ) );
      m_candidateWidth = 0;
      if ( !m_collapsed )
         Resize( m_expandedWidth, Height() );
   }

   // Called from the focus timer. Resize events are not used to store the
   // width: on macOS they arrive asynchronously, so the intermediate sizes of
   // collapsing, expanding and layout updates cannot be told apart from user
   // resizes. A width is only taken over once the expanded window has kept it
   // for two consecutive timer ticks; all programmatic changes are completed
   // within a single event handler.
   void TrackExpandedWidth()
   {
      if ( !m_savePositionEnabled || m_collapsed )
      {
         m_candidateWidth = 0;
         return;
      }
      int width = Width();
      if ( width <= 0 )
         return;
      if ( width != m_candidateWidth )
      {
         m_candidateWidth = width;
         return;
      }
      if ( width != m_expandedWidth )
      {
         m_expandedWidth = width;
         SaveWindowWidth();
      }
   }

   std::vector<std::unique_ptr<SearchEntry>> m_entries;

   void InitializeControls()
   {
      m_gui = std::make_unique<GUIData>( *this );

      m_gui->search.SetScaledMinWidth( ExpandedSearchWidth );
      m_gui->search.OnGetFocus( (Control::event_handler)&FinderInterface::e_SearchGetFocus, *this );
      m_gui->search.OnTextUpdated(
         (Edit::text_event_handler)&FinderInterface::e_TextUpdated, *this );
      m_gui->search.OnKeyPress(
         (Control::keyboard_event_handler)&FinderInterface::e_KeyPress, *this );

      m_gui->results.SetNumberOfColumns( 1 );
      m_gui->results.HideHeader();
      m_gui->results.DisableRootDecoration();
      m_gui->results.DisableMultipleSelections();
      m_gui->results.EnableAlternateRowColor();
      m_gui->results.SetScaledIconSize( 16 );
      m_gui->results.SetScaledMinWidth( ExpandedSearchWidth );
      m_gui->results.OnNodeActivated(
         (TreeBox::node_event_handler)&FinderInterface::e_NodeActivated, *this );
      m_gui->results.OnKeyPress(
         (Control::keyboard_event_handler)&FinderInterface::e_KeyPress, *this );

      m_gui->sizer.SetMargin( 8 );
      m_gui->sizer.SetSpacing( 6 );
      m_gui->sizer.Add( m_gui->search );
      m_gui->sizer.Add( m_gui->results, 100 );
      SetSizer( m_gui->sizer );
      SetWindowTitle( "Finder" );
      OnMove( (Control::move_event_handler)&FinderInterface::e_WindowMoved, *this );
      AdjustToContents();
      RestoreWindowWidth();

      // Estimated until the first result row can be measured.
      int rowHeight = Max( m_gui->results.Font().Height(), LogicalPixelsToPhysical( 16 ) ) + LogicalPixelsToPhysical( 4 );
      SetResultsHeight( VisibleResultRows * rowHeight + LogicalPixelsToPhysical( 4 ) );

      // PCL provides no window activation event, so the focus is polled.
      m_focusTimer = std::make_unique<Timer>();
      m_focusTimer->SetInterval( 0.25 );
      m_focusTimer->SetPeriodic();
      m_focusTimer->OnTimer( (Timer::timer_event_handler)&FinderInterface::e_FocusTimer, *this );
      m_focusTimer->Start();
   }

   void LoadEntries()
   {
      for ( const pcl::Process& process : pcl::Process::AllProcesses() )
      {
         auto entry = std::make_unique<SearchEntry>();
         entry->type = EntryType::Process;
         entry->processId = process.Id();
         entry->label = process.Id();
         entry->icon = process.SmallIcon();
         if ( entry->icon.IsNull() )
            entry->icon = process.Icon();
         m_entries.push_back( std::move( entry ) );
      }

      size_type firstScript = m_entries.size();
      for ( const String& path : ScriptSearchPaths() )
         EnumerateScripts( path, m_entries );

      // Scripts show their #feature-icon; scripts without a (loadable) icon
      // fall back to a core-provided document icon.
      int iconSize = m_gui->results.LogicalPixelsToPhysical( 16 );
      for ( size_type i = firstScript; i < m_entries.size(); ++i )
      {
         SearchEntry& entry = *m_entries[i];
         if ( !entry.iconPath.IsEmpty() )
            try
            {
               if ( File::ExtractExtension( entry.iconPath ).CompareIC( ".svg" ) == 0 )
                  entry.icon = Bitmap::FromSVGFile( entry.iconPath, iconSize, iconSize );
               else
               {
                  entry.icon = Bitmap( entry.iconPath );
                  if ( !entry.icon.IsEmpty() && (entry.icon.Width() != iconSize || entry.icon.Height() != iconSize) )
                     entry.icon = entry.icon.ScaledToSize( iconSize, iconSize );
               }
            }
            catch ( ... )
            {
               entry.icon = Bitmap();
            }

         if ( entry.icon.IsNull() || entry.icon.IsEmpty() )
         {
            entry.icon = Bitmap( ScaledResource( ":/icons/document.png" ) );
            if ( entry.icon.IsEmpty() )
               Console().WarningLn( "<end><cbr>** Finder: script icon resource could not be loaded." );
         }
      }

      std::sort( m_entries.begin(), m_entries.end(),
         []( const std::unique_ptr<SearchEntry>& a, const std::unique_ptr<SearchEntry>& b )
         {
            int order = a->label.CompareIC( b->label );
            if ( order != 0 )
               return order < 0;
            return a->type < b->type;
         } );
   }

   // Each tree node's index equals the position of its entry in m_visibleEntries.
   std::vector<size_type> m_visibleEntries;

   void RefreshResults()
   {
      String query = m_gui->search.Text().Trimmed();
      m_gui->results.DisableUpdates();
      m_gui->results.Clear();
      m_visibleEntries.clear();

      if ( !query.IsEmpty() )
         for ( size_type i = 0; i < m_entries.size(); ++i )
         {
            const SearchEntry& entry = *m_entries[i];
            // Only the process or script name is searched.
            if ( entry.label.FindFirstIC( query ) == String::notFound )
               continue;

            m_visibleEntries.push_back( i );
            TreeBox::Node* node = new TreeBox::Node( m_gui->results );
            node->SetText( 0, entry.label );
            node->SetIcon( 0, entry.icon );
            node->SetToolTip( 0, (entry.type == EntryType::Script) ? entry.scriptPath : String( entry.processId ) );
         }

      if ( m_gui->results.NumberOfChildren() > 0 )
         m_gui->results.SetCurrentNode( m_gui->results.Child( 0 ) );
      m_gui->results.EnableUpdates();

      // The result list is only shown while there is a query.
      if ( m_resultsShown == query.IsEmpty() )
      {
         m_resultsShown = !query.IsEmpty();
         m_gui->results.SetVisible( m_resultsShown && !m_collapsed );
         UpdateWindowHeight();
      }

      if ( !m_resultsHeightMeasured && m_gui->results.NumberOfChildren() > 0 )
      {
         int rowHeight = m_gui->results.NodeRect( m_gui->results.Child( 0 ) ).Height();
         int frame = m_gui->results.Height() - m_gui->results.Viewport().Height();
         if ( rowHeight > 0 && frame >= 0 && frame <= LogicalPixelsToPhysical( 16 ) )
         {
            m_resultsHeightMeasured = true;
            SetResultsHeight( VisibleResultRows * rowHeight + frame );
         }
      }
   }

   void MoveSelection( int delta )
   {
      int count = m_gui->results.NumberOfChildren();
      if ( count == 0 )
         return;
      int index = 0;
      if ( const TreeBox::Node* current = m_gui->results.CurrentNode() )
         index = m_gui->results.ChildIndex( current ) + delta;
      index = Range( index, 0, count - 1 );
      m_gui->results.SetCurrentNode( m_gui->results.Child( index ) );
   }

   // The selected entry is only remembered here and launched from a timer,
   // once the key or activation event has returned. Launching directly from
   // the event handler deleted the result nodes while the TreeBox was still
   // dispatching their event, and ran scripts (with their modal dialogs) in a
   // nested event loop inside the search line's key handler.
   void ActivateCurrent( bool closeWindow = false )
   {
      if ( m_launchPending )
         return;
      const TreeBox::Node* current = m_gui->results.CurrentNode();
      if ( current == nullptr )
         return;
      int index = m_gui->results.ChildIndex( current );
      if ( index < 0 || size_type( index ) >= m_visibleEntries.size() )
         return;

      // Copied, so that nothing refers to the result list once it is cleared.
      const SearchEntry& entry = *m_entries[m_visibleEntries[size_type( index )]];
      m_pendingType = entry.type;
      m_pendingProcessId = entry.processId;
      m_pendingScriptPath = entry.scriptPath;
      m_pendingClose = closeWindow;
      m_launchPending = true;

      if ( m_launchTimer == nullptr )
      {
         m_launchTimer = std::make_unique<Timer>();
         m_launchTimer->SetSingleShot();
         m_launchTimer->SetInterval( 0 );
         m_launchTimer->OnTimer( (Timer::timer_event_handler)&FinderInterface::e_LaunchTimer, *this );
      }
      m_launchTimer->Start();
   }

   // The search window either closes or stays open behind the launched
   // process or script, with an empty query and no result list.
   void LaunchPending()
   {
      ClearSearch();
      if ( m_pendingClose )
         Hide();

      if ( m_pendingType == EntryType::Process )
      {
         if ( !pcl::Process( m_pendingProcessId ).Launch() )
            throw Error( "The process " + String( m_pendingProcessId ) + " could not be launched." );
      }
      else
         Console().ExecuteScript( m_pendingScriptPath );
   }

   // No exception may leave an event handler: the dispatcher does not catch
   // it, so it would reach the core and terminate PixInsight.
   // Periodic handlers only log, so that a persistent error cannot open one
   // message box after another.
   template <typename F>
   void Guarded( F f, bool showDialog = true )
   {
      try
      {
         f();
      }
      catch ( const Exception& error )
      {
         ShowError( error.Message(), showDialog );
      }
      catch ( const std::exception& error )
      {
         ShowError( String( error.what() ), showDialog );
      }
      catch ( ... )
      {
         ShowError( "Unknown error.", showDialog );
      }
   }

   static void ShowError( const String& message, bool showDialog )
   {
      try
      {
         Console().CriticalLn( "<end><cbr>** Finder: " + message );
         if ( showDialog )
            MessageBox( message, "Finder", StdIcon::Error ).Execute();
      }
      catch ( ... )
      {
      }
   }

   void e_LaunchTimer( Timer& )
   {
      Guarded( [this]() { LaunchPending(); } );
      m_launchPending = false;
   }

   void e_StartupTimer( Timer& )
   {
      Guarded( [this]()
      {
         m_startupTimer->Stop();
         ProcessInterface::Launch();
      } );
   }

   void e_FocusTimer( Timer& )
   {
      Guarded( [this]() { UpdateFocusState(); }, false/*showDialog*/ );
   }

   void UpdateFocusState()
   {
      if ( m_visibilityCheckPending && IsVisible() )
      {
         m_visibilityCheckPending = false;
         IsoString key = SettingsKey() + "Geometry/";
         pcl::Point savedPosition = Position();
         bool hasSavedPosition = Settings::Read( key + "Left", savedPosition.x ) &&
                                 Settings::Read( key + "Top", savedPosition.y );
         // Applied again once shown, in case the first Show() changed the size.
         RestoreWindowWidth();
         RestoreVisiblePosition( *this, hasSavedPosition ? &savedPosition : nullptr );
         m_savePositionEnabled = true;
         SaveWindowPosition( Position() );
      }
      if ( !IsVisible() )
         return;
      TrackExpandedWidth();
      bool active = IsActiveWindow();
      if ( m_windowActive && !active && MinimizeWithoutFocus() )
         Collapse();
      m_windowActive = active;
   }

   void e_WindowMoved( Control&, const pcl::Point& newPosition, const pcl::Point& )
   {
      if ( m_savePositionEnabled )
         Guarded( [&]() { SaveWindowPosition( newPosition ); } );
   }

   // Clicking into the search line (or activating the window) expands it.
   void e_SearchGetFocus( Control& )
   {
      Guarded( [this]() { Expand(); } );
   }

   void e_TextUpdated( Edit&, const String& )
   {
      Guarded( [this]() { RefreshResults(); } );
   }

   void e_NodeActivated( TreeBox&, TreeBox::Node&, int )
   {
      Guarded( [this]() { ActivateCurrent( m_launchedFromInstance ); } );
   }

   void e_KeyPress( Control&, int key, unsigned, bool& wantsKey )
   {
      wantsKey = true;
      Guarded( [&]()
      {
         switch ( key )
         {
         case KeyCode::Return:
            ActivateCurrent( m_launchedFromInstance || CloseAfterLaunch() );
            break;
         case KeyCode::Up:
            MoveSelection( -1 );
            break;
         case KeyCode::Down:
            MoveSelection( +1 );
            break;
         case KeyCode::PageUp:
            MoveSelection( -10 );
            break;
         case KeyCode::PageDown:
            MoveSelection( +10 );
            break;
         case KeyCode::Escape:
            // Escape only clears the query; the window is never closed by it.
            ClearSearch();
            break;
         default:
            wantsKey = false;
            break;
         }
      } );
   }
};

ProcessInterface* FinderProcess::DefaultInterface() const
{
   return TheFinderInterface;
}

void FinderModule::OnLoad()
{
   if ( TheFinderInterface != nullptr )
      TheFinderInterface->ScheduleStartupLaunch();
}

bool FinderInstance::ExecuteGlobal()
{
   return TheFinderInterface != nullptr && TheFinderInterface->LaunchFromInstance();
}

} // namespace

PCL_MODULE_EXPORT int InstallPixInsightModule( int mode )
{
   const char* stage = "module registration";
   try
   {
      new FinderModule;

      if ( mode == InstallMode::FullInstall )
      {
         stage = "process registration";
         TheFinderProcess = new FinderProcess;
         stage = "interface registration";
         TheFinderInterface = new FinderInterface;
      }

      return 0;
   }
   catch ( const Exception& error )
   {
      Error( String( "Finder failed during " ) + stage + ": " + error.Message() ).Show();
   }
   catch ( const std::exception& error )
   {
      Error( String( "Finder failed during " ) + stage + ": " + error.what() ).Show();
   }
   catch ( ... )
   {
      Error( String( "Finder failed during " ) + stage +
             " with a non-standard exception." ).Show();
   }

   return 1;
}
