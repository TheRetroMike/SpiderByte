/*
 * W.J. van der Laan 2011-2012
 */
#include "bitcoingui.h"
#include "clientmodel.h"
#include "walletmodel.h"
#include "optionsmodel.h"
#include "guiutil.h"
#include "guiconstants.h"
#include "splash.h"
#include <string.h>

#include "init.h"
#include "ui_interface.h"
#include "qtipcserver.h"

#include <stdint.h>

#if QT_VERSION < 0x060000
#include <QDesktopWidget>
#include <QTextCodec>
#include <QRegExp>
#endif

#include <QApplication>
#include <QStyleFactory>
#include <QMessageBox>
#include <QLocale>
#include <QTranslator>
#include <QSplashScreen>
#include <QLibraryInfo>
#include <QEvent>
#include <QCloseEvent>
#include <QLabel>
#include <QTextTable>
//#include <QTextCursor>
#include <QVBoxLayout>
#include <QObject>

#if defined(BITCOIN_NEED_QT_PLUGINS) && !defined(_BITCOIN_QT_PLUGINS_INCLUDED)
#define _BITCOIN_QT_PLUGINS_INCLUDED
#define __INSURE__
#include <QtPlugin>
Q_IMPORT_PLUGIN(qcncodecs)
Q_IMPORT_PLUGIN(qjpcodecs)
Q_IMPORT_PLUGIN(qtwcodecs)
Q_IMPORT_PLUGIN(qkrcodecs)
Q_IMPORT_PLUGIN(qtaccessiblewidgets)
#endif

using namespace std;

// Need a global reference for the notifications to find the GUI
// By Simone: BitcoinGUI is accessible from outside too, removes static
BitcoinGUI *guiref;
Splash *stwref;

/** by Simone: "Shutdown" window */
ShutdownWindow::ShutdownWindow(QWidget *parent, Qt::WindowFlags f):
    QWidget(parent, f)
{
    QVBoxLayout *layout = new QVBoxLayout();
	QLabel *l = new QLabel();
	setWindowTitle(tr("SpiderByte"));
	QString s = tr("SpiderByte Wallet is shutting down, please wait...") + "<br><br>" + tr("DO NOT shutdown the computer until this window disappears");
	l->setText(s);
	//l->setStyleSheet("QLabel { background-color : grey; color : black; }");
	setStyleSheet("QWidget { background-color : lightgrey; color: black; }");
    layout->addWidget(l);
    setLayout(layout);
}
void ShutdownWindow::showShutdownWindow()
{
    // Center shutdown window in the screen

#if QT_VERSION < 0x060000
	QRect screenGeometry = QApplication::desktop()->screenGeometry();
#else
	QScreen* screen = QGuiApplication::primaryScreen();
	QRect screenGeometry = screen->availableGeometry();
#endif

	int x = (screenGeometry.width() - width()) / 2;
	int y = (screenGeometry.height() - height()) / 2;
	move(x, y);
    show();
}

void ShutdownWindow::closeEvent(QCloseEvent *event)
{
    event->ignore();
}


static void ThreadSafeMessageBox(const std::string& message, const std::string& caption, int style)
{
    // Message from network thread
    if(guiref)
    {
        bool modal = (style & CClientUIInterface::MODAL);
        // in case of modal message, use blocking connection to wait for user to click OK
        QMetaObject::invokeMethod(guiref, "error",
                                   modal ? GUIUtil::blockingGUIThreadConnection() : Qt::QueuedConnection,
                                   Q_ARG(QString, QString::fromStdString(caption)),
                                   Q_ARG(QString, QString::fromStdString(message)),
                                   Q_ARG(bool, modal));
    }
    else
    {
        printf("%s: %s\n", caption.c_str(), message.c_str());
        fprintf(stderr, "%s: %s\n", caption.c_str(), message.c_str());
    }
}

static bool ThreadSafeAskFee(int64 nFeeRequired, const std::string& strCaption)
{
    if(!guiref)
        return false;
    if(nFeeRequired < MIN_TX_FEE || nFeeRequired <= nTransactionFee || fDaemon)
        return true;
    bool payFee = false;

    QMetaObject::invokeMethod(guiref, "askFee", GUIUtil::blockingGUIThreadConnection(),
                               Q_ARG(qint64, nFeeRequired),
                               Q_ARG(bool*, &payFee));

    return payFee;
}

static void ThreadSafeHandleURI(const std::string& strURI)
{
    if(!guiref)
        return;

    QMetaObject::invokeMethod(guiref, "handleURI", GUIUtil::blockingGUIThreadConnection(),
                               Q_ARG(QString, QString::fromStdString(strURI)));
}

// by Simone: the new found sync speed may render the interface a bit stutter, let's run this when necessary
void RefreshQtGui()
{
	QApplication::instance()->processEvents();
}

// find splash font color here
static void InitMessage(const std::string &message)
{
	if (stwref)
	{
		stwref->setMessage(message.c_str());
	}
}

static void QueueShutdown()
{
    QMetaObject::invokeMethod(QCoreApplication::instance(), "quit", Qt::QueuedConnection);
}

/*
   Translate string to current locale using Qt.
 */
static std::string Translate(const char* psz)
{
    return QCoreApplication::translate("bitcoin-core", psz).toStdString();
}

/* Handle runaway exceptions. Shows a message box with the problem and quits the program.
 */
ShutdownWindow *sdwRef;
static void handleRunawayException(std::exception *e)
{
    PrintExceptionContinue(e, "Runaway exception");
    QMessageBox::critical(0, "Runaway exception", BitcoinGUI::tr("A fatal error occurred. SpiderByte can no longer continue safely and will quit.") + QString("\n\n") + QString::fromStdString(strMiscWarning));
    exit(1);
}

#ifndef BITCOIN_QT_TEST
int main(int argc, char *argv[])
{
    // Do this early as we don't want to bother initializing if we are just calling IPC
    ipcScanRelay(argc, argv);

    // Internal string conversion is all UTF-8
    //QTextCodec::setCodecForTr(QTextCodec::codecForName("UTF-8"));
    //QTextCodec::setCodecForCStrings(QTextCodec::codecForTr());

    Q_INIT_RESOURCE(bitcoin);

// by Simone: still allow compiling even with QT4
#if QT_VERSION >= 0x050F00
	QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
#endif

	QApplication::setStyle(QStyleFactory::create("Fusion"));

    QApplication app(argc, argv);

    // Install global event filter that makes sure that long tooltips can be word-wrapped
    app.installEventFilter(new GUIUtil::ToolTipToRichTextFilter(TOOLTIP_WRAP_THRESHOLD, &app));

    // Command-line options take precedence:
    ParseParameters(argc, argv);

    // ... then bitcoin.conf:
    if (!boost::filesystem::is_directory(GetDataDir(false)))
    {
        // This message can not be translated, as translation is not initialized yet
        // (which not yet possible because lang=XX can be overridden in bitcoin.conf in the data directory)
        QMessageBox::critical(0, "SpiderByte",
                              QString("Error: Specified data directory \"%1\" does not exist.").arg(QString::fromStdString(mapArgs["-datadir"])));
        return 1;
    }
    ReadConfigFile(mapArgs, mapMultiArgs);

    // Application identification (must be set before OptionsModel is initialized,
    // as it is used to locate QSettings)
    app.setOrganizationName("SpiderByte");
    app.setOrganizationDomain("SpiderByte.su");
    if(GetBoolArg("-testnet")) // Separate UI settings for testnet
        app.setApplicationName("SpiderByte-Qt-testnet");
    else
        app.setApplicationName("SpiderByte-Qt");

    // ... then GUI settings:
    OptionsModel optionsModel;

    // Get desired locale (e.g. "de_DE") from command line or use system locale
    QString lang_territory = QString::fromStdString(GetArg("-lang", QLocale::system().name().toStdString()));
    QString lang = lang_territory;
    // Convert to "de" only by truncating "_DE"
    lang.truncate(lang_territory.lastIndexOf('_'));

    QTranslator qtTranslatorBase, qtTranslator, translatorBase, translator;
    // Load language files for configured locale:
    // - First load the translator for the base language, without territory
    // - Then load the more specific locale translator

    // Load e.g. qt_de.qm
    if (qtTranslatorBase.load("qt_" + lang, QLibraryInfo::location(QLibraryInfo::TranslationsPath)))
        app.installTranslator(&qtTranslatorBase);

    // Load e.g. qt_de_DE.qm
    if (qtTranslator.load("qt_" + lang_territory, QLibraryInfo::location(QLibraryInfo::TranslationsPath)))
        app.installTranslator(&qtTranslator);

    // Load e.g. bitcoin_de.qm (shortcut "de" needs to be defined in bitcoin.qrc)
    if (translatorBase.load(lang, ":/translations/"))
        app.installTranslator(&translatorBase);

    // Load e.g. bitcoin_de_DE.qm (shortcut "de_DE" needs to be defined in bitcoin.qrc)
    if (translator.load(lang_territory, ":/translations/"))
        app.installTranslator(&translator);

    // Subscribe to global signals from core
    uiInterface.ThreadSafeMessageBox.connect(ThreadSafeMessageBox);
    uiInterface.ThreadSafeAskFee.connect(ThreadSafeAskFee);
    uiInterface.ThreadSafeHandleURI.connect(ThreadSafeHandleURI);
    uiInterface.InitMessage.connect(InitMessage);
    uiInterface.QueueShutdown.connect(QueueShutdown);
    uiInterface.Translate.connect(Translate);

    // Show help message immediately after parsing command-line options (for "-lang") and setting locale,
    // but before showing splash screen.
    if (mapArgs.count("-?") || mapArgs.count("--help"))
    {
        GUIUtil::HelpMessageBox help;
        help.showOrPrint();
        return 1;
    }

	// by Simone: Splash
	Splash stw;
	stwref = &stw;
	stw.showSplash();
	app.processEvents();
	Sleep(100);
    app.processEvents();
    app.setQuitOnLastWindowClosed(false);
    try
    {
        // Regenerate startup link, to fix links to old versions
        if (GUIUtil::GetStartOnSystemStartup())
            GUIUtil::SetStartOnSystemStartup(true);

        ShutdownWindow sdw;
		sdwRef = &sdw;
        BitcoinGUI window;
        guiref = &window;
        if(AppInit2())
        {
            {
                // Put this in a block, so that the Model objects are cleaned up before
                // calling Shutdown().

                optionsModel.Upgrade(); // Must be done after AppInit2

                ClientModel clientModel(&optionsModel);
                WalletModel walletModel(pwalletMain, &optionsModel);

                window.setClientModel(&clientModel);
                window.setWalletModel(&walletModel);

				// by Simone: hide startup Window, startup completed
				stw.hideSplash();

				// by Simone: load skin here
				window.loadSkin();

                // If -min option passed, start window minimized.
                if(GetBoolArg("-min"))
                {
                    window.showMinimized();
                }
                else
                {
                    window.show();
                }

                // Place this here as guiref has to be defined if we don't want to lose URIs
                ipcInit(argc, argv);

                app.exec();

				// by Simone: shutdown Window
				sdw.showShutdownWindow();
				app.processEvents();
				Sleep(100);
				app.processEvents();

	            window.hide();
	            window.setClientModel(0);
	            window.setWalletModel(0);
                guiref = 0;
            }
            // Shutdown the core and its threads, but don't exit Bitcoin-Qt here
            Shutdown(NULL);
        }
        else
        {
            return 1;
        }
    } catch (std::exception& e) {
        handleRunawayException(&e);
    } catch (...) {
        handleRunawayException(NULL);
    }

    return 0;
}
#endif // BITCOIN_QT_TEST
