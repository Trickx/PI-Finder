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

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <memory>
#include <vector>

#define MODULE_VERSION_MAJOR     1
#define MODULE_VERSION_MINOR     0
#define MODULE_VERSION_REVISION  0
#define MODULE_VERSION_BUILD     0
#define MODULE_VERSION_LANGUAGE  eng

using namespace pcl;

#ifndef PIXINSIGHT_SCRIPT_DIR
#  ifdef __PCL_MACOSX
#    define PIXINSIGHT_SCRIPT_DIR "/Applications/PixInsight/src/scripts"
#  else
#    define PIXINSIGHT_SCRIPT_DIR ""
#  endif
#endif

namespace
{

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
   AddUniquePath( paths, String( PIXINSIGHT_SCRIPT_DIR ) );

   String home = File::HomeDirectory();
   AddUniquePath( paths, home + "/PixInsight/Scripts" );
   AddUniquePath( paths, home + "/Documents/PixInsight/Scripts" );

   // Additional roots can be provided as a colon-separated list.
   if ( const char* extraPaths = std::getenv( "PIXINSIGHT_SCRIPT_PATHS" ) )
   {
      String list( extraPaths );
      while ( !list.IsEmpty() )
      {
         String path;
         size_type separator = list.FindFirst( ':' );
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

// Resolves the #feature-icon directive of a script. Returns an empty string
// if the script declares no icon or the icon file does not exist.
String ScriptIconPath( const String& scriptPath )
{
   IsoString iconSpec;
   IsoStringList lines;
   File::ReadTextFile( scriptPath ).Break( lines, '\n' );
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
      if ( (extension.FindFirstIC( ".js" ) == 0 && extension.Length() == 3) ||
           (extension.FindFirstIC( ".scp" ) == 0 && extension.Length() == 4) )
      {
         String scriptPath = File::FullPath( path );
         bool alreadyIndexed = std::any_of( entries.begin(), entries.end(),
            [&scriptPath]( const std::unique_ptr<SearchEntry>& entry )
            {
               return entry->type == EntryType::Script && entry->scriptPath == scriptPath;
            } );
         if ( alreadyIndexed )
            continue;

         auto entry = std::make_unique<SearchEntry>();
         entry->type = EntryType::Script;
         entry->scriptPath = scriptPath;
         try
         {
            entry->iconPath = ScriptIconPath( scriptPath );
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
   InterfaceFeatures Features() const override
   {
      return InterfaceFeature::DragObject | InterfaceFeature::PreferencesButton;
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
            InitializeControls();

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

   void SaveSettings() const override
   {
      if ( m_gui != nullptr )
         SaveGeometry();
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
   bool m_windowActive = true;
   bool m_launchedFromInstance = false;
   bool m_executingInstance = false;
   bool m_collapsed = false;
   int m_expandedWidth = 0;
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
   void UpdateWindowHeight()
   {
      if ( m_collapsed )
         return;
      int width = Width();
      SetVariableHeight();
      AdjustToContents();
      SetFixedHeight();
      Resize( Max( width, Width() ), Height() );
   }

   // Without focus the window shrinks to a narrow search line.
   void Collapse()
   {
      if ( m_collapsed )
         return;
      m_collapsed = true;
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
      Resize( Max( m_expandedWidth, Width() ), Height() );
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
      AdjustToContents();
      RestoreGeometry();
      // Only the position is restored; every session starts at the default width.
      Resize( LogicalPixelsToPhysical( ExpandedSearchWidth + 2*8 ), Height() );

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

   void ActivateCurrent( bool closeWindow = false )
   {
      const TreeBox::Node* current = m_gui->results.CurrentNode();
      if ( current == nullptr )
         return;
      int index = m_gui->results.ChildIndex( current );
      if ( index < 0 || size_type( index ) >= m_visibleEntries.size() )
         return;

      // The search window either closes or stays open behind the launched
      // process or script, with an empty query and no result list.
      const SearchEntry& entry = *m_entries[m_visibleEntries[size_type( index )]];
      ClearSearch();
      if ( closeWindow )
         Hide();
      if ( entry.type == EntryType::Process )
      {
         if ( !pcl::Process( entry.processId ).Launch() )
            MessageBox( "The selected process could not be launched.",
                        "Finder", StdIcon::Error ).Execute();
         return;
      }

      try
      {
         Console().ExecuteScript( entry.scriptPath );
      }
      catch ( const Error& error )
      {
         MessageBox( error.Message(), "Finder", StdIcon::Error ).Execute();
      }
   }

   void e_StartupTimer( Timer& )
   {
      m_startupTimer->Stop();
      ProcessInterface::Launch();
   }

   void e_FocusTimer( Timer& )
   {
      if ( !IsVisible() )
         return;
      bool active = IsActiveWindow();
      if ( m_windowActive && !active && MinimizeWithoutFocus() )
         Collapse();
      m_windowActive = active;
   }

   // Clicking into the search line (or activating the window) expands it.
   void e_SearchGetFocus( Control& )
   {
      Expand();
   }

   void e_TextUpdated( Edit&, const String& )
   {
      RefreshResults();
   }

   void e_NodeActivated( TreeBox&, TreeBox::Node&, int )
   {
      ActivateCurrent( m_launchedFromInstance );
   }

   void e_KeyPress( Control&, int key, unsigned, bool& wantsKey )
   {
      wantsKey = true;
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
