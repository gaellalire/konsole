/*
    This file is part of the KDE system
    Copyright (C)  1999,2000 Boloni Laszlo

    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public License
    along with this library; see the file COPYING.LIB.  If not, write to
    the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
    Boston, MA 02111-1307, USA.

    konsole_part.h
 */

#include "konsole_part.h"

#include <sys/stat.h>
#include <stdlib.h>

#include <qfile.h>
#include <qlayout.h>
#include <qwmatrix.h> 

#include <kaboutdata.h>
#include <kdebug.h>
#include <kfontdialog.h>
#include <kglobalsettings.h>
#include <kiconloader.h>
#include <klineeditdlg.h>
#include <klocale.h>
#include <kmessagebox.h>
#include <krun.h>

extern "C"
{
  /**
   * This function is the 'main' function of this part.  It takes
   * the form 'void *init_lib<library name>()  It always returns a
   * new factory object
   */
  void *init_libkonsolepart()
  {
      kdDebug(1211) << "Konsole in actions!!!" << endl;
    return new konsoleFactory;
  }
};

/**
 * We need one static instance of the factory for our C 'main' function
 */
KInstance *konsoleFactory::s_instance = 0L;
KAboutData *konsoleFactory::s_aboutData = 0;

konsoleFactory::konsoleFactory()
{
}

konsoleFactory::~konsoleFactory()
{
  if (s_instance)
    delete s_instance;

  if ( s_aboutData )
    delete s_aboutData;

  s_instance = 0;
  s_aboutData = 0;
}

KParts::Part *konsoleFactory::createPartObject(QWidget *parentWidget, const char *widgetName,
                                         QObject *parent, const char *name, const char*,
                                         const QStringList& )
{
  kdDebug(1211) << "konsoleFactory::createPart parentWidget=" << parentWidget << " parent=" << parent << endl;
  KParts::Part *obj = new konsolePart(parentWidget, widgetName, parent, name);
  return obj;
}

KInstance *konsoleFactory::instance()
{
  if ( !s_instance )
    {
      s_aboutData = new KAboutData("konsole", I18N_NOOP("Konsole"), "1.1");
      s_instance = new KInstance( s_aboutData );
    }
  return s_instance;
}

//////////////////////////////////////////////////////////////////////

class KonsoleFontSelectAction : public KSelectAction {
public:
    KonsoleFontSelectAction(const QString &text, int accel,
                            const QObject* receiver, const char* slot,
                            QObject* parent, const char* name = 0 )
        : KSelectAction(text, accel, receiver, slot, parent, name) {}
    KonsoleFontSelectAction( const QString &text, const QIconSet& pix,
                             int accel, const QObject* receiver,
                             const char* slot, QObject* parent,
                             const char* name = 0 )
        : KSelectAction(text, pix, accel, receiver, slot, parent, name) {}

    virtual void slotActivated( int index );
};

void KonsoleFontSelectAction::slotActivated(int index) {
    // emit even if it's already activated
    if (currentItem() == index) {
      KSelectAction::slotActivated();
      return;
    } else {
      KSelectAction::slotActivated(index);
    }
}

//////////////////////////////////////////////////////////////////////

const char *fonts[] = {
 "13", 
 "7",   // tiny font, never used
 "10",  // small font
 "13",  // medium
 "15",  // large
 "20", // huge
 "-misc-console-medium-r-normal--16-160-72-72-c-160-iso10646-1", // "Linux"
 "-misc-fixed-medium-r-normal--15-140-75-75-c-90-iso10646-1",    // "Unicode"
 };

#define TOPFONT (sizeof(fonts)/sizeof(char*))
#define DEFAULTFONT TOPFONT

#define DEFAULT_HISTORY_SIZE 1000

konsolePart::konsolePart(QWidget *_parentWidget, const char *widgetName, QObject *parent, const char *name)
  : KParts::ReadOnlyPart(parent, name)
,te(0)
,se(0)
,colors(0)
,rootxpm(0)
,m_histSize(DEFAULT_HISTORY_SIZE)
{
  parentWidget=_parentWidget;
  setInstance(konsoleFactory::instance());

  // This is needed since only konsole.cpp does it
  // Without those two -> crash on keypress... (David)
  KeyTrans::loadAll();

  QStrList eargs;

  const char* shell = getenv("SHELL");
  if (shell == NULL || *shell == '\0') shell = "/bin/sh";
  eargs.append(shell);
  te = new TEWidget(parentWidget,widgetName);
  te->setMinimumSize(150,70);    // allow resizing, cause resize in TEWidget

  setWidget(te);
  // faking a KMainwindow - TESession assumes that (wrong design!)
  se = new TESession((KMainWindow*)parentWidget,te,shell,eargs,"xterm");
  connect( se,SIGNAL(done(TESession*,int)),
           this,SLOT(doneSession(TESession*,int)) );
  connect( te,SIGNAL(configureRequest(TEWidget*,int,int,int)),
           this,SLOT(configureRequest(TEWidget*,int,int,int)) );
  se->setConnect(TRUE);
  te->currentSession = se;

  rootxpm = new KRootPixmap(te);

  colors = new ColorSchemaList();
  colors->checkSchemas();

  readProperties();

  makeGUI();

  updateSchemaMenu();

  ColorSchema *sch=colors->find(s_schema);
  if (sch)
    curr_schema=sch->numb();
  else
    curr_schema = 0;
  for (uint i=0; i<m_schema->count(); i++)
    m_schema->setItemChecked(i,false);

  m_schema->setItemChecked(curr_schema,true);
  se->setSchemaNo(curr_schema);

  // insert keymaps into menu
  for (int i = 0; i < KeyTrans::count(); i++) {
    KeyTrans* ktr = KeyTrans::find(i);
    m_keytab->insertItem(ktr->hdr(),ktr->numb());
  }

  applySettingsToGUI();

  // kDebugInfo("Loading successful");
  se->run();

  connect( se, SIGNAL( destroyed() ), this, SLOT( sessionDestroyed() ) );
}

void konsolePart::doneSession(TESession*,int)
{
  // see doneSession in konsole.cpp
  if (se)
  {
    kdDebug(1211) << "doneSession - disconnecting done" << endl;;
    disconnect( se,SIGNAL(done(TESession*,int)),
                this,SLOT(doneSession(TESession*,int)) );
    se->setConnect(FALSE);
    //QTimer::singleShot(100,se,SLOT(terminate()));
    kdDebug(1211) << "se->terminate()" << endl;;
    se->terminate();
  }
}

void konsolePart::sessionDestroyed()
{
  kdDebug(1211) << "sessionDestroyed()" << endl;;
  disconnect( se, SIGNAL( destroyed() ), this, SLOT( sessionDestroyed() ) );
  se = 0;
  delete this;
}

void konsolePart::configureRequest(TEWidget*te,int,int x,int y)
{
  sendRMBclickAtX=x;
  sendRMBclickAtY=y;

  m_popupMenu->popup(te->mapToGlobal(QPoint(x,y)));
}

konsolePart::~konsolePart()
{
  kdDebug(1211) << "konsolePart::~konsolePart() this=" << this << endl;
  if ( se ) {
    disconnect( se, SIGNAL( destroyed() ), this, SLOT( sessionDestroyed() ) );
    kdDebug(1211) << "Deleting se session" << endl;
    delete se;
    se=0;
  }

  if (colors) delete colors;
  colors=0;

  //te is deleted by the framework
}

bool konsolePart::openURL( const KURL & url )
{
  m_url = url;
  emit setWindowCaption( url.prettyURL() );
  kdDebug(1211) << "Set Window Caption to " << url.prettyURL() << "\n";
  emit started( 0 );

  if ( url.isLocalFile() ) {
      struct stat buff;
      stat( QFile::encodeName( url.path() ), &buff );
      QString text = ( S_ISDIR( buff.st_mode ) ? url.path() : url.directory() );
      KRun::shellQuote(text);
      text = QString::fromLatin1("cd ") + text + '\n';
      QKeyEvent e(QEvent::KeyPress, 0,-1,0, text);
      se->getEmulation()->onKeyPress(&e);
  }

  emit completed();
  return true;
}

void konsolePart::makeGUI()
{
  // Send Signal Menu -------------------------------------------------------------
  m_signals = new KPopupMenu((KMainWindow*)parentWidget);
  m_signals->insertItem( i18n( "&Suspend Task" )   + " (STOP)", 17);     // FIXME: comes with 3 values
  m_signals->insertItem( i18n( "&Continue Task" )  + " (CONT)", 18);     // FIXME: comes with 3 values
  m_signals->insertItem( i18n( "&Hangup" )         + " (HUP)",   1);
  m_signals->insertItem( i18n( "&Interrupt Task" ) + " (INT)",   2);
  m_signals->insertItem( i18n( "&Terminate Task" ) + " (TERM)", 15);
  m_signals->insertItem( i18n( "&Kill Task" )      + " (KILL)",  9);
  connect(m_signals, SIGNAL(activated(int)), SLOT(sendSignal(int)));

  // Settings Menu ----------------------------------------------------------------
  m_options = new KPopupMenu((KMainWindow*)parentWidget);

  // Frame on/off
  showFrame = new KToggleAction(i18n("Show &Frame"), 0,
                                this, SLOT(slotToggleFrame()), this);
  showFrame->plug(m_options);

  // Scrollbar
  selectScrollbar = new KSelectAction(i18n("Scro&llbar"), 0, this,
                                      SLOT(slotSelectScrollbar()), this);
  QStringList scrollitems;
  scrollitems << i18n("&Hide") << i18n("&Left") << i18n("&Right");
  selectScrollbar->setItems(scrollitems);
  selectScrollbar->plug(m_options);
  m_options->insertSeparator();

  // Select font
  selectFont = new KonsoleFontSelectAction( i18n( "F&ont" ), SmallIconSet( "text" ), 0,
                                            this, SLOT(slotSelectFont()), this);
  QStringList it;
  it << i18n("&Normal")
     << i18n("&Tiny")
     << i18n("&Small")
     << i18n("&Medium")
     << i18n("&Large")
     << i18n("&Huge")
     << ""
     << i18n("&Linux")
     << i18n("&Unicode")
     << ""
     << i18n("&Custom...");
  selectFont->setItems(it);
  selectFont->plug(m_options);

  // Keyboard Options Menu ---------------------------------------------------
  m_keytab = new KPopupMenu((KMainWindow*)parentWidget);
  m_keytab->setCheckable(TRUE);
  connect(m_keytab, SIGNAL(activated(int)), SLOT(keytab_menu_activated(int)));
  m_options->insertItem( SmallIconSet( "key_bindings" ), i18n( "&Keyboard" ), m_keytab );

  // Schema Options Menu -----------------------------------------------------
  m_schema = new KPopupMenu((KMainWindow*)parentWidget);
  m_schema->setCheckable(TRUE);
  connect(m_schema, SIGNAL(activated(int)), SLOT(schema_menu_activated(int)));
  connect(m_schema, SIGNAL(aboutToShow()), SLOT(schema_menu_check()));
  m_options->insertItem( SmallIconSet( "colorize" ), i18n( "Sch&ema" ), m_schema);

  m_options->insertSeparator();
  KAction *historyType = new KAction(i18n("&History..."), "history", 0, this,
                                     SLOT(slotHistoryType()), this);
  historyType->plug(m_options);

  // Select Bell
  m_options->insertSeparator();
  selectBell = new KSelectAction(i18n("&Bell"), SmallIconSet( "bell"), 0 , this,
                                 SLOT(slotSelectBell()), this);
  QStringList bellitems;
  bellitems << i18n("&None")
            << i18n("&System Notification")
            << i18n("&Visible Bell");
  selectBell->setItems(bellitems);
  selectBell->plug(m_options);

  // Select line spacing
  selectLineSpacing =
    new KSelectAction
    (
     i18n("Li&ne Spacing"),
     SmallIconSet("leftjust"), // Non-code hack.
     0,
     this,
     SLOT(slotSelectLineSpacing()),
     this
    );

  QStringList lineSpacingList;

  lineSpacingList
    << i18n("&0")
    << i18n("&1")
    << i18n("&2")
    << i18n("&3")
    << i18n("&4")
    << i18n("&5")
    << i18n("&6")
    << i18n("&7")
    << i18n("&8");

  selectLineSpacing->setItems(lineSpacingList);
  selectLineSpacing->plug(m_options);

  // Blinking Cursor
  blinkingCursor = new KToggleAction (i18n("Blinking &Cursor"),
                                      0, this,SLOT(slotBlinkingCursor()), this);
  blinkingCursor->plug(m_options);

  // Word Connectors
  KAction *WordSeps = new KAction(i18n("Wor&d Connectors..."), 0, this,
                                  SLOT(slotWordSeps()), this);
  WordSeps->plug(m_options);

  // Save Settings
  m_options->insertSeparator();
  KAction *saveSettings = new KAction( i18n("Save &Settings"), "filesave", 0, this,
					     SLOT(saveProperties()), this);
  saveSettings->plug(m_options);
  m_options->insertTearOffHandle();

  // Popup Menu -------------------------------------------------------------------
  m_popupMenu = new KPopupMenu((KMainWindow*)parentWidget);
  KAction *pasteClipboard = new KAction(i18n("&Paste"), "editpaste", 0,
                                        te, SLOT(pasteClipboard()), this);
  pasteClipboard->plug(m_popupMenu);
  m_popupMenu->insertSeparator();

  KAction *sendRMBclick = new KAction(i18n("Send R&ight Click"), 0, this,
                                      SLOT(slotSendRMBclick()), this);
  sendRMBclick->plug(m_popupMenu);

  m_popupMenu->insertItem(i18n("&Send Signal"), m_signals);
  m_popupMenu->insertSeparator();

  m_popupMenu->insertItem(i18n("S&ettings"), m_options);
  m_popupMenu->insertSeparator();

  KAction *closeSession = new KAction(i18n("&Close Terminal Emulator"), "fileclose", 0, this,
                                      SLOT(closeCurrentSession()), this);
  closeSession->plug(m_popupMenu);
  m_popupMenu->insertTearOffHandle();
}

void konsolePart::applySettingsToGUI()
{
  showFrame->setChecked( b_framevis );
  selectScrollbar->setCurrentItem(n_scroll);
  selectFont->setCurrentItem(n_font);
  updateKeytabMenu();
  selectBell->setCurrentItem(n_bell);
  selectLineSpacing->setCurrentItem(te->lineSpacing());
  blinkingCursor->setChecked(te->blinkingCursor());
  m_schema->setItemChecked(curr_schema,true);
};

void konsolePart::readProperties()
{
  KConfig* config = new KConfig("konsolepartrc",TRUE);
  config->setDesktopGroup();

  b_framevis = config->readBoolEntry("has frame",FALSE);
  b_histEnabled = config->readBoolEntry("historyenabled",true);
  n_bell = QMIN(config->readUnsignedNumEntry("bellmode",TEWidget::BELLSYSTEM),2);
  n_font = QMIN(config->readUnsignedNumEntry("font",3),TOPFONT);
  n_keytab=config->readNumEntry("keytab",0); // act. the keytab for this session
  n_scroll = QMIN(config->readUnsignedNumEntry("scrollbar",TEWidget::SCRRIGHT),2);
  m_histSize = config->readNumEntry("history",DEFAULT_HISTORY_SIZE);
  s_word_seps= config->readEntry("wordseps",":@-./_~");

  QFont tmpFont("fixed");
  defaultFont = config->readFontEntry("defaultfont", &tmpFont);
  setFont(QMIN(config->readUnsignedNumEntry("font",3),TOPFONT));

  QString schema = config->readEntry("Schema");

  s_kconfigSchema=config->readEntry("schema", "");
  ColorSchema* sch = colors->find(schema.isEmpty() ? s_kconfigSchema : schema);
  if (!sch) {
    sch=(ColorSchema*)colors->at(0);  //the default one
  }
  if (sch->hasSchemaFileChanged()) sch->rereadSchemaFile();
  s_schema = sch->relPath();
  curr_schema = sch->numb();
  pmPath = sch->imagePath();
  te->setColorTable(sch->table()); //FIXME: set twice here to work around a bug

  if (sch->useTransparency()) {
    //KONSOLEDEBUG << "Setting up transparency" << endl;
    rootxpm->setFadeEffect(sch->tr_x(), QColor(sch->tr_r(), sch->tr_g(), sch->tr_b()));
    rootxpm->start();
    rootxpm->repaint(true);
  }
  else {
    rootxpm->stop();
    pixmap_menu_activated(sch->alignment());
  }

  if (b_histEnabled && m_histSize)
    se->setHistory(HistoryTypeBuffer(m_histSize));
  else if (b_histEnabled && !m_histSize)
    se->setHistory(HistoryTypeFile());
  else
    se->setHistory(HistoryTypeNone());

  se->setKeymapNo(n_keytab);
  te->setBellMode(n_bell);
  te->setBlinkingCursor(config->readBoolEntry("BlinkingCursor",FALSE));
  te->setFrameStyle( b_framevis?(QFrame::WinPanel|QFrame::Sunken):QFrame::NoFrame );
  te->setLineSpacing( config->readUnsignedNumEntry( "LineSpacing", 0 ) );
  te->setScrollbarLocation(n_scroll);
  te->setWordCharacters(s_word_seps);

  delete config;

  config = new KConfig("konsolerc",TRUE);
  config->setDesktopGroup();
  te->setTerminalSizeHint( config->readBoolEntry("TerminalSizeHint",true) );
  delete config;
}

void konsolePart::saveProperties()
{
  KConfig* config = new KConfig("konsolepartrc");
  config->setDesktopGroup();

  config->writeEntry("bellmode",n_bell);
  config->writeEntry("BlinkingCursor", te->blinkingCursor());
  config->writeEntry("defaultfont", defaultFont);
  config->writeEntry("font",n_font);
  config->writeEntry("history", se->history().getSize());
  config->writeEntry("historyenabled", b_histEnabled);
  config->writeEntry("keytab",n_keytab);
  config->writeEntry("has frame",b_framevis);
  config->writeEntry("LineSpacing", te->lineSpacing());
  config->writeEntry("schema",s_kconfigSchema);
  config->writeEntry("scrollbar",n_scroll);
  config->writeEntry("wordseps",s_word_seps);

  config->sync();
  delete config;
}

void konsolePart::sendSignal(int sn)
{
  if (se) se->sendSignal(sn);
}

void konsolePart::slotSendRMBclick() 
{
  te->sendRMBclick(sendRMBclickAtX,sendRMBclickAtY);
}

void konsolePart::closeCurrentSession()
{
  sendSignal(SIGHUP);
}

void konsolePart::slotToggleFrame() 
{
  b_framevis = showFrame->isChecked();
  te->setFrameStyle( b_framevis?(QFrame::WinPanel|QFrame::Sunken):QFrame::NoFrame);
}

void konsolePart::slotSelectScrollbar() 
{
  n_scroll = selectScrollbar->currentItem();
  te->setScrollbarLocation(n_scroll);
}

void konsolePart::slotSelectFont() {
  int item = selectFont->currentItem();
  // KONSOLEDEBUG << "slotSelectFont " << item << endl;
  if (item == DEFAULTFONT) {
    if ( KFontDialog::getFont(defaultFont, true) == QDialog::Rejected ) {
      selectFont->setCurrentItem(n_font);
      return;
    }
  }
  setFont(item);
}

void konsolePart::setFont(int fontno)
{
  QFont f;
  if (fontno == DEFAULTFONT)
    f = defaultFont;
  else
  if (fonts[fontno][0] == '-')
    f.setRawName( fonts[fontno] );
  else {
    f.setFamily("fixed");
    f.setFixedPitch(true);
    f.setPixelSize(QString(fonts[fontno]).toInt());
  }
  if ( !f.exactMatch() && fontno != DEFAULTFONT) {
    QString msg = i18n("Font `%1' not found.\nCheck README.linux.console for help.").arg(fonts[fontno]);
    KMessageBox::error((KMainWindow*)parentWidget, msg);
    return;
  }
  se->setFontNo(fontno);
  te->setVTFont(f);
  n_font = fontno;
}

void konsolePart::updateKeytabMenu()
{
  m_keytab->setItemChecked(n_keytab,FALSE);
  m_keytab->setItemChecked(se->keymapNo(),TRUE);
  n_keytab = se->keymapNo();
}

void konsolePart::keytab_menu_activated(int item)
{
  se->setKeymapNo(item);
  updateKeytabMenu();
}

void konsolePart::schema_menu_activated(int item)
{
  setSchema(item);
  s_kconfigSchema = s_schema; // This is the new default
}

void konsolePart::schema_menu_check()
{
  if (colors->checkSchemas()) {
    updateSchemaMenu();
  }
}

void konsolePart::updateSchemaMenu()
{
  m_schema->clear();
  for (int i = 0; i < (int) colors->count(); i++)  {
    ColorSchema* s = (ColorSchema*)colors->at(i);
    m_schema->insertItem(s->title(),s->numb(),0);
  }

  if (te && te->currentSession) {
    m_schema->setItemChecked(te->currentSession->schemaNo(),true);
  }
}

void konsolePart::setSchema(int numb)
{
  ColorSchema* s = colors->find(numb);
  if (!s) {
    kdWarning() << "No schema found. Using default." << endl;
    s=(ColorSchema*)colors->at(0);
  }
  if (s->numb() != numb)  {
    kdWarning() << "No schema with number " << numb << endl;
  }

  if (s->hasSchemaFileChanged()) {
    const_cast<ColorSchema *>(s)->rereadSchemaFile();
  }
  if (s) setSchema(s);
}

void konsolePart::setSchema(ColorSchema* s)
{
  if (!s) return;

  if (m_schema) {
    m_schema->setItemChecked(curr_schema,FALSE);
    m_schema->setItemChecked(s->numb(),TRUE);
  }

  s_schema = s->relPath();
  curr_schema = s->numb();
  pmPath = s->imagePath();
  te->setColorTable(s->table()); //FIXME: set twice here to work around a bug

  if (s->useTransparency()) {
    rootxpm->setFadeEffect(s->tr_x(), QColor(s->tr_r(), s->tr_g(), s->tr_b()));
    rootxpm->start();
    rootxpm->repaint(true);
  }
  else {
    rootxpm->stop();
    pixmap_menu_activated(s->alignment());
  }

  te->setColorTable(s->table());
  se->setSchemaNo(s->numb());
}

void konsolePart::pixmap_menu_activated(int item)
{
  if (item <= 1) pmPath = "";
  QPixmap pm(pmPath);
  if (pm.isNull()) {
    pmPath = "";
    item = 1;
    te->setBackgroundColor(te->getDefaultBackColor());
    return;
  }
  // FIXME: respect scrollbar (instead of te->size)
  n_render= item;
  switch (item) {
    case 1: // none
    case 2: // tile
            te->setBackgroundPixmap(pm);
    break;
    case 3: // center
            { QPixmap bgPixmap;
              bgPixmap.resize(te->size());
              bgPixmap.fill(te->getDefaultBackColor());
              bitBlt( &bgPixmap, ( te->size().width() - pm.width() ) / 2,
                                ( te->size().height() - pm.height() ) / 2,
                      &pm, 0, 0,
                      pm.width(), pm.height() );

              te->setBackgroundPixmap(bgPixmap);
            }
    break;
    case 4: // full
            {
              float sx = (float)te->size().width() / pm.width();
              float sy = (float)te->size().height() / pm.height();
              QWMatrix matrix;
              matrix.scale( sx, sy );
              te->setBackgroundPixmap(pm.xForm( matrix ));
            }
    break;
    default: // oops
             n_render = 1;
  }
}

void konsolePart::slotHistoryType()
{
  HistoryTypeDialog dlg(se->history(), m_histSize, (KMainWindow*)parentWidget);
  if (dlg.exec()) {
    if (dlg.isOn()) {
      if (dlg.nbLines() > 0) {
        se->setHistory(HistoryTypeBuffer(dlg.nbLines()));
        m_histSize = dlg.nbLines();
        b_histEnabled = true;
      } 
      else {
        se->setHistory(HistoryTypeFile());
        m_histSize = 0;
        b_histEnabled = true;
      }
    }
    else {
      se->setHistory(HistoryTypeNone());
      m_histSize = dlg.nbLines();
      b_histEnabled = false;
    }
  }
}

void konsolePart::slotSelectBell() {
  n_bell = selectBell->currentItem();
  te->setBellMode(n_bell);
}

void konsolePart::slotSelectLineSpacing()
{
  te->setLineSpacing( selectLineSpacing->currentItem() );
}

void konsolePart::slotBlinkingCursor()
{
  te->setBlinkingCursor(blinkingCursor->isChecked());
}

void konsolePart::slotWordSeps() {
  KLineEditDlg dlg(i18n("Characters other than alphanumerics considered part of a word when double clicking"),s_word_seps, (KMainWindow*)parentWidget);
  dlg.setCaption(i18n("Word Connectors"));
  if (dlg.exec()) {
    s_word_seps = dlg.text();
    te->setWordCharacters(s_word_seps);
  }
}

//////////////////////////////////////////////////////////////////////

HistoryTypeDialog::HistoryTypeDialog(const HistoryType& histType,
                                     unsigned int histSize,
                                     QWidget *parent)
  : KDialogBase(Plain, i18n("History Configuration"),
                Help | Default | Ok | Cancel, Ok,
                parent)
{
  QFrame *mainFrame = plainPage();

  QHBoxLayout *hb = new QHBoxLayout(mainFrame);

  m_btnEnable    = new QCheckBox(i18n("&Enable"), mainFrame);

  QObject::connect(m_btnEnable, SIGNAL(toggled(bool)),
                   this,      SLOT(slotHistEnable(bool)));

  m_size = new QSpinBox(0, 10 * 1000 * 1000, 100, mainFrame);
  m_size->setValue(histSize);
  m_size->setSpecialValueText(i18n("Unlimited (number of lines)", "Unlimited"));

  hb->addWidget(m_btnEnable);
  hb->addWidget(new QLabel(i18n("Number of lines : "), mainFrame));
  hb->addWidget(m_size);

  if ( ! histType.isOn()) {
    m_btnEnable->setChecked(false);
    slotHistEnable(false);
  } else {
    m_btnEnable->setChecked(true);
    m_size->setValue(histType.getSize());
    slotHistEnable(true);
  }
  setHelp("configure-history");
}

void HistoryTypeDialog::slotDefault()
{
  m_btnEnable->setChecked(true);
  m_size->setValue(DEFAULT_HISTORY_SIZE);
  slotHistEnable(true);
}

void HistoryTypeDialog::slotHistEnable(bool b)
{
  m_size->setEnabled(b);
  if (b) m_size->setFocus();
}

unsigned int HistoryTypeDialog::nbLines() const
{
  return m_size->value();
}

bool HistoryTypeDialog::isOn() const
{
  return m_btnEnable->isChecked();
}


#include "konsole_part.moc"
