#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLocale>
#include <QTranslator>
#include <QTimer>
#include <QStyleFactory>
#include <QPixmapCache>
#include <QProxyStyle>
#include <QStyleOptionButton>
#include <QStyleOptionToolButton>
#include <QPainter>
#include <QSettings>
#include <QStandardPaths>
#include <QSysInfo>
#include "MainWindow.h"
#include "CliToolsConfig.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

// Increases the gap between icon and text in QPushButton / QToolButton from Qt's
// hardcoded 4 px to kIconTextSpacing.
class SpratStyle : public QProxyStyle {
public:
    SpratStyle() : QProxyStyle(QStyleFactory::create("Fusion")) {}

    void drawControl(ControlElement element, const QStyleOption* opt,
                     QPainter* p, const QWidget* w) const override
    {
        if (element == CE_PushButtonLabel) {
            if (const auto* btn = qstyleoption_cast<const QStyleOptionButton*>(opt))
                if (!btn->icon.isNull() && !btn->text.isEmpty()) {
                    drawIconLabel(btn->rect, btn->icon, btn->iconSize, btn->text,
                                  btn->fontMetrics, btn->palette, btn->state, p, w);
                    return;
                }
        }
        if (element == CE_ToolButtonLabel) {
            if (const auto* tb = qstyleoption_cast<const QStyleOptionToolButton*>(opt))
                if (!tb->icon.isNull() && !tb->text.isEmpty()
                        && tb->toolButtonStyle == Qt::ToolButtonTextBesideIcon) {
                    drawIconLabel(tb->rect, tb->icon, tb->iconSize, tb->text,
                                  tb->fontMetrics, tb->palette, tb->state, p, w);
                    return;
                }
        }
        QProxyStyle::drawControl(element, opt, p, w);
    }

private:
    static constexpr int kIconTextSpacing = 8;

    void drawIconLabel(const QRect& r, const QIcon& icon, const QSize& isz,
                       const QString& text, const QFontMetrics& fm,
                       const QPalette& pal, QStyle::State state,
                       QPainter* p, const QWidget* w) const
    {
        const QIcon::Mode mode =
            (state & State_Enabled)
                ? ((state & State_HasFocus) ? QIcon::Active : QIcon::Normal)
                : QIcon::Disabled;
        const QIcon::State istate = (state & State_On) ? QIcon::On : QIcon::Off;
        const qreal dpr = w ? w->devicePixelRatio() : 1.0;
        const QPixmap pm = icon.pixmap(isz, dpr, mode, istate);
        const int pw = qRound(pm.width()  / pm.devicePixelRatio());
        const int ph = qRound(pm.height() / pm.devicePixelRatio());

        uint tf = Qt::AlignVCenter | Qt::AlignLeft | Qt::TextShowMnemonic;
        if (!proxy()->styleHint(SH_UnderlineShortcut, nullptr, w))
            tf |= Qt::TextHideMnemonic;

        const int tw = fm.boundingRect(r, tf, text).width();
        const QRect iconRect(r.x() + (r.width() - pw - kIconTextSpacing - tw) / 2,
                             r.y() + (r.height() - ph) / 2,
                             pw, ph);
        p->drawPixmap(iconRect, pm);

        QRect textRect = r;
        textRect.setLeft(iconRect.right() + kIconTextSpacing);
        proxy()->drawItemText(p, textRect, tf, pal,
                              bool(state & State_Enabled), text, QPalette::ButtonText);
    }
};

// Checked by --smoke-test once the main window has had time to appear.
static constexpr int SMOKE_TEST_DELAY_MS = 3000;
// How long --smoke-test-zip may take to extract the ZIP and lay out its sprites.
static constexpr int SMOKE_TEST_IMPORT_TIMEOUT_MS = 60000;

// Exit codes for --smoke-test, so CI logs say which check failed.
enum SmokeTestResult {
    SMOKE_TEST_PASSED = 0,
    SMOKE_TEST_WINDOW_NOT_SHOWN = 2,
    SMOKE_TEST_CLI_MISSING = 3,
    SMOKE_TEST_IMPORT_FAILED = 4,
    SMOKE_TEST_IMPORT_TIMED_OUT = 5,
    SMOKE_TEST_QUIT_HUNG = 6,
};

// How long the app may take to quit once the smoke test has its result.
static constexpr int SMOKE_TEST_QUIT_GRACE_MS = 10000;

// Writes straight to stderr: Windows GUI builds drop qInfo() output when stderr is redirected.
static void smokeTestLog(const QString& message) {
    std::fprintf(stderr, "[smoke-test] %s\n", message.toLocal8Bit().constData());
    std::fflush(stderr);
}

/// Points every QStandardPaths location at Qt's test folders and clears sprat's settings there,
/// so a smoke test sees a fresh install's defaults and never touches the user's own settings.
static void isolateSmokeTestSettings() {
    QStandardPaths::setTestModeEnabled(true);
    const QString configDir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    QDir(QDir(configDir).filePath(QStringLiteral("sprat"))).removeRecursively();

    // Skip the first-run "choose a projects folder" prompt: onboarding isn't under test,
    // and on macOS its native folder panel can't be closed by QCoreApplication::exit().
    // Test mode doesn't redirect DocumentsLocation, so keep the folder inside the test config.
    const QString projectsFolder = QDir(configDir).filePath(QStringLiteral("sprat/Sprat Projects"));
    QDir().mkpath(projectsFolder);
    QSettings settings(CliToolsConfig::configPath(), QSettings::IniFormat);
    settings.setValue(QStringLiteral("settings/default_projects_folder"), projectsFolder);
    settings.sync();

    smokeTestLog(QStringLiteral("settings dir: %1/sprat (fresh)").arg(configDir));
}

/// Ends the smoke test with result. If the app hasn't quit SMOKE_TEST_QUIT_GRACE_MS later
/// (a native modal loop or a hang during shutdown), reports that and exits with
/// SMOKE_TEST_QUIT_HUNG instead of leaving CI to time out.
static void finishSmokeTest(int result) {
    QCoreApplication::exit(result);
    std::thread([result]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(SMOKE_TEST_QUIT_GRACE_MS));
        smokeTestLog(QStringLiteral("FAIL: the app did not quit within %1 s of the result (%2)")
                         .arg(SMOKE_TEST_QUIT_GRACE_MS / 1000)
                         .arg(result));
        std::_Exit(SMOKE_TEST_QUIT_HUNG);
    }).detach();
}

/// Checks that the main window is on screen and, when requireCli is set, that the
/// sprat-cli tools, transforms and profiles config can be found.
/// @return A SmokeTestResult exit code
static int runSmokeTestChecks(const QWidget& mainWindow, bool requireCli) {
    smokeTestLog(QStringLiteral("platform: %1").arg(QGuiApplication::platformName()));
    // The slice of a universal macOS binary that is running (x86_64 under Rosetta).
    smokeTestLog(QStringLiteral("architecture: %1").arg(QSysInfo::buildCpuArchitecture()));
    smokeTestLog(QStringLiteral("application dir: %1").arg(QCoreApplication::applicationDirPath()));

    if (!mainWindow.isVisible()) {
        smokeTestLog(QStringLiteral("FAIL: main window is not visible"));
        return SMOKE_TEST_WINDOW_NOT_SHOWN;
    }

    const CliPaths cliPaths = CliToolsConfig::loadCliPaths();
    const QString transformsDir = CliToolsConfig::queryTransformsDir(cliPaths.convertBinary);
    const QString profilesConfig = CliToolsConfig::queryDefaultProfilesConfig(cliPaths.layoutBinary);
    const bool transformsFound = !transformsDir.isEmpty() && QFileInfo(transformsDir).isDir();
    const bool profilesFound = !profilesConfig.isEmpty() && QFileInfo(profilesConfig).isFile();
    smokeTestLog(QStringLiteral("transforms dir: %1 (%2)")
                     .arg(transformsDir, transformsFound ? "found" : "missing"));
    smokeTestLog(QStringLiteral("profiles config: %1 (%2)")
                     .arg(profilesConfig, profilesFound ? "found" : "missing"));

    if (requireCli && (!transformsFound || !profilesFound)) {
        smokeTestLog(QStringLiteral("FAIL: sprat-cli assets not found"));
        return SMOKE_TEST_CLI_MISSING;
    }

    return SMOKE_TEST_PASSED;
}

/// Opens zipPath through the GUI with the given layout profile, then exits with a SmokeTestResult.
/// @param expectedSprites Sprite count the layout must have, or -1 for any non-zero count
static void runSmokeTestImport(MainWindow& mainWindow, const QString& zipPath, const QString& profile,
                               const QStringList& folders, int expectedSprites) {
    smokeTestLog(QStringLiteral("importing %1 with profile '%2'").arg(zipPath, profile));

    auto* timeout = new QTimer(&mainWindow);
    timeout->setSingleShot(true);
    QObject::connect(timeout, &QTimer::timeout, &mainWindow, []() {
        smokeTestLog(QStringLiteral("FAIL: import did not finish within %1 s (a dialog may be open)")
                         .arg(SMOKE_TEST_IMPORT_TIMEOUT_MS / 1000));
        finishSmokeTest(SMOKE_TEST_IMPORT_TIMED_OUT);
    });
    timeout->start(SMOKE_TEST_IMPORT_TIMEOUT_MS);

    mainWindow.runSmokeTestImport(zipPath, profile, folders,
        [timeout, expectedSprites](const MainWindow::SmokeTestImportResult& result) {
            timeout->stop();
            if (!result.pickedFolders.isEmpty()) {
                smokeTestLog(QStringLiteral("folders picked: %1")
                                 .arg(result.pickedFolders.join(QStringLiteral(", "))));
            }
            smokeTestLog(QStringLiteral("profile in use: %1").arg(result.profile));
            smokeTestLog(QStringLiteral("sprites: %1, atlases: %2")
                             .arg(result.spriteCount)
                             .arg(result.atlasSizes.join(QStringLiteral(", "))));
            QString error = result.error;
            if (error.isEmpty() && expectedSprites >= 0 && result.spriteCount != expectedSprites) {
                error = QStringLiteral("expected %1 sprites, got %2").arg(expectedSprites).arg(result.spriteCount);
            }
            if (!error.isEmpty()) {
                smokeTestLog(QStringLiteral("FAIL: %1").arg(error));
                finishSmokeTest(SMOKE_TEST_IMPORT_FAILED);
                return;
            }
            smokeTestLog(QStringLiteral("PASS"));
            finishSmokeTest(SMOKE_TEST_PASSED);
        });
}

/// Returns the values after every occurrence of option in arguments.
static QStringList smokeTestOptionValues(const QStringList& arguments, const QString& option) {
    QStringList values;
    for (int i = 0; i + 1 < arguments.size(); ++i) {
        if (arguments.at(i) == option) values << arguments.at(i + 1);
    }
    return values;
}

/// Returns the value after option in arguments, or defaultValue when it is absent.
static QString smokeTestOptionValue(const QStringList& arguments, const QString& option,
                                    const QString& defaultValue = QString()) {
    const QStringList values = smokeTestOptionValues(arguments, option);
    return values.isEmpty() ? defaultValue : values.first();
}

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

extern "C" {
    EMSCRIPTEN_KEEPALIVE
    void sync_idbfs() {
        // Safe check to avoid Asyncify collisions
        EM_ASM(
            if (typeof Asyncify !== 'undefined' && Asyncify.currData) return;
            if (typeof Module !== 'undefined' && Module.asm && Module.asm.async_status && Module.asm.async_status() !== 0) return;
            if (typeof FS !== 'undefined' && FS.syncfs) {
                if (FS.syncing) return;
                FS.syncfs(false, function (err) {
                    if (err) console.error('FS.syncfs error:', err);
                });
            }
        );
    }
    bool jsIsAsyncBusy();
}
#endif

int main(int argc, char *argv[]) {
    // Force a safe style to avoid crashes from unavailable styles like 'kvantum'
    // This must be done before QApplication is created
    qputenv("QT_STYLE_OVERRIDE", "Fusion");

    QApplication app(argc, argv);
    app.setStyle(new SpratStyle());

    const bool smokeTest = QCoreApplication::arguments().contains(QStringLiteral("--smoke-test"));
    if (smokeTest) {
        isolateSmokeTestSettings();
    }

    // Increase pixmap cache to prevent thrashing on sprite-heavy projects
    // Default is 10 MB; 256 MB is better for projects with 500+ sprites
    QPixmapCache::setCacheLimit(256 * 1024); // 256 MB in KB

#ifdef __EMSCRIPTEN__
    // Disable Asyncify's overly strict "multiple async operations" checks.
    // This allows resize and other events to work without false-positive assertions.
    // See: https://stackoverflow.com/questions/76878211
    EM_ASM(
        if (typeof Asyncify !== 'undefined') {
            Asyncify.whenDone = () => Promise.resolve();
            console.log("[Sprat] Asyncify checks disabled");
        }
        // Add global error handler to catch exceptions before they crash
        window.addEventListener('error', function(e) {
            console.error('[sprat] Uncaught error:', e.error, e.message);
            e.preventDefault();
            return true;
        }, true);
        window.addEventListener('unhandledrejection', function(e) {
            console.error('[sprat] Unhandled promise rejection:', e.reason);
            e.preventDefault();
            return true;
        }, true);
        // Ensure keyboard input focus and forward regular keys to Qt
        function ensureCanvasFocus() {
            if (Module.canvas) {
                Module.canvas.focus();
                // Remove focus from any input elements to ensure canvas gets key events
                if (document.activeElement && document.activeElement !== Module.canvas) {
                    if (document.activeElement.blur) {
                        document.activeElement.blur();
                    }
                }
            }
        }
        // Attach listeners directly to the canvas to catch events before Qt consumes them
        if (Module.canvas) {
            Module.canvas.addEventListener('click', function(e) {
                console.log("[Click] on canvas");
                setTimeout(ensureCanvasFocus, 0);
            }, true);

            Module.canvas.addEventListener('mousedown', function(e) {
                console.log("[MouseDown] on canvas");
                setTimeout(ensureCanvasFocus, 0);
            }, true);
        }

        document.addEventListener('click', function(e) {
            console.log("[Click] on document");
            setTimeout(ensureCanvasFocus, 0);
        }, true);
        document.addEventListener('focus', function() {
            setTimeout(ensureCanvasFocus, 0);
        }, true);
        window.addEventListener('focus', ensureCanvasFocus, true);

        // Workaround: Forward non-modifier keys that Qt doesn't receive and prevent browser defaults
        document.addEventListener('keydown', function(e) {
            var key = e.key;
            var code = e.code;
            var isModifier = /^(Control|Shift|Alt|Meta)/.test(key);

            console.log("[KeyDown] key=" + key + " code=" + code);

            // Keys that we always want to prevent browser defaults for
            var keysToPrevent = [
                'Delete', 'Backspace',
                'ArrowUp', 'ArrowDown', 'ArrowLeft', 'ArrowRight',
                'Home', 'End', 'Enter', 'Escape', ' '
            ];
            var shouldPrevent = keysToPrevent.indexOf(key) !== -1;

            // Prevent browser defaults for keys the app handles
            if (shouldPrevent) {
                e.preventDefault();
            }

            // Prevent browser zoom shortcuts (Ctrl+0, Ctrl+1, etc)
            if ((e.ctrlKey || e.metaKey) && /^[-0-9=]/.test(key)) {
                e.preventDefault();
            }

            // Ensure canvas has focus
            if (!isModifier && Module.canvas && document.activeElement !== Module.canvas) {
                setTimeout(ensureCanvasFocus, 0);
            }
        }, true);

        // Drag event interception for non-file content (Qt WASM crash workaround)
        // is handled by sprat_install_drop_handlers() in WasmFileDialog.cpp,
        // which also handles file drops and URL drops.
    );
#endif

    // Initialize CLI tools configuration after a generous delay
    QTimer::singleShot(1000, []() {
#ifdef __EMSCRIPTEN__
        if (jsIsAsyncBusy()) {
            QTimer::singleShot(500, []() { CliToolsConfig::ensureConfigExists(); });
            return;
        }
#endif
        CliToolsConfig::ensureConfigExists();
    });

    // Set up internationalization
    QTranslator translator;
    const QString systemLocale = QLocale::system().name();
    const QString translationBaseName = QString("sprat-gui_%1").arg(systemLocale);
    app.installTranslator(&translator);

    const QStringList translationDirectories = {
        ":/i18n",
        QCoreApplication::applicationDirPath() + "/i18n",
        QCoreApplication::applicationDirPath(),
        QDir::currentPath() + "/i18n"
    };

    for (const QString& directory : translationDirectories) {
        if (translator.load(translationBaseName, directory)) {
            break;
        }
    }

    // Create main window
    MainWindow mainWindow;
    if (smokeTest) {
        // An update dialog would interrupt the test and try to replace the build under test.
        mainWindow.setUpdateCheckEnabled(false);
    }

    // Show window and ensure it has focus for keyboard input
    QTimer::singleShot(100, &mainWindow, [&mainWindow]() {
        mainWindow.show();
        mainWindow.setFocus();
        mainWindow.activateWindow();
    });

    // --smoke-test lets CI check that a built or packaged app starts and shows its window,
    // and with --smoke-test-zip, that it can open a ZIP of images and lay out a spritesheet.
    const QStringList arguments = QCoreApplication::arguments();
    if (smokeTest) {
        const bool requireCli = arguments.contains(QStringLiteral("--require-cli"));
        const QString zipPath = smokeTestOptionValue(arguments, QStringLiteral("--smoke-test-zip"));
        const QString profile = smokeTestOptionValue(arguments, QStringLiteral("--smoke-test-profile"),
                                                     QStringLiteral("fast"));
        const QStringList folders = smokeTestOptionValues(arguments, QStringLiteral("--smoke-test-folder"));
        bool expectedSpritesGiven = false;
        int expectedSprites = smokeTestOptionValue(arguments, QStringLiteral("--smoke-test-expect-sprites"))
                                  .toInt(&expectedSpritesGiven);
        if (!expectedSpritesGiven) expectedSprites = -1;
        QTimer::singleShot(SMOKE_TEST_DELAY_MS, &mainWindow,
                           [&mainWindow, requireCli, zipPath, profile, folders, expectedSprites]() {
            const int result = runSmokeTestChecks(mainWindow, requireCli);
            if (result != SMOKE_TEST_PASSED || zipPath.isEmpty()) {
                if (result == SMOKE_TEST_PASSED) smokeTestLog(QStringLiteral("PASS"));
                // exit() also leaves any Qt modal dialog's event loop opened during startup.
                finishSmokeTest(result);
                return;
            }
            runSmokeTestImport(mainWindow, zipPath, profile, folders, expectedSprites);
        });
    }

    // Start Qt event loop
    return app.exec();
}
