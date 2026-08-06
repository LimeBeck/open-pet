#include "spikecontroller.h"

#include <LayerShellQt/Window>

#include <QElapsedTimer>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>

namespace {

int envInt(const char *name, int fallback)
{
    bool ok = false;
    const int value = qEnvironmentVariableIntValue(name, &ok);
    return ok ? value : fallback;
}

LayerShellQt::Window::Anchors anchorsFromEnv()
{
    const QString corner = qEnvironmentVariable("OPENPET_ANCHOR", QStringLiteral("bottom-right"));
    using Window = LayerShellQt::Window;

    // LayerShellQt объявляет флаги без Q_DECLARE_OPERATORS_FOR_FLAGS,
    // поэтому `|` между значениями enum пришлось бы приводить вручную.
    const auto combine = [](Window::Anchor a, Window::Anchor b) {
        return Window::Anchors(a) | b;
    };

    if (corner == QLatin1String("bottom-left"))
        return combine(Window::AnchorBottom, Window::AnchorLeft);
    if (corner == QLatin1String("top-right"))
        return combine(Window::AnchorTop, Window::AnchorRight);
    if (corner == QLatin1String("top-left"))
        return combine(Window::AnchorTop, Window::AnchorLeft);
    return combine(Window::AnchorBottom, Window::AnchorRight);
}

} // namespace

int main(int argc, char *argv[])
{
    // Отсчёт до появления питомца начинается здесь, чтобы в него попала
    // и инициализация Qt, а не только наша часть работы.
    QElapsedTimer startupClock;
    startupClock.start();

    // Начиная с Qt 6.5 отдельная инициализация LayerShellQt::Shell не нужна:
    // достаточно настроить окно через LayerShellQt::Window до показа.
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("openpet-spike-overlay"));

    SpikeController controller;
    controller.startStartupClock(startupClock.nsecsElapsed());
    controller.setMode(RegionMode(envInt("OPENPET_REGION", int(RegionMode::AlphaOnce))));

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("spike"), &controller);
    engine.rootContext()->setContextProperty(QStringLiteral("hudVisible"), envInt("OPENPET_HUD", 1) != 0);
    engine.loadFromModule("OpenPetSpike", "Main");

    if (engine.rootObjects().isEmpty())
        return 1;

    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
    if (!window) {
        qFatal("Корневой объект QML не является Window");
        return 1;
    }

    // Прозрачный фон: без этого layer-shell поверхность будет залита чёрным
    // и вопрос про клики по прозрачным пикселям потеряет смысл.
    window->setColor(Qt::transparent);

    using LayerWindow = LayerShellQt::Window;
    LayerWindow *layer = LayerWindow::get(window);
    layer->setLayer(LayerWindow::LayerTop);
    layer->setAnchors(anchorsFromEnv());
    layer->setMargins(QMargins(0, 0, envInt("OPENPET_MARGIN_RIGHT", 8), envInt("OPENPET_MARGIN_BOTTOM", 8)));
    // Ноль, а не -1: питомец не резервирует место и не двигает чужие окна,
    // но и не лезет под панель.
    layer->setExclusiveZone(0);
    layer->setKeyboardInteractivity(LayerWindow::KeyboardInteractivityNone);
    layer->setScope(QStringLiteral("open-pet-spike"));
    layer->setCloseOnDismissed(false);

    window->show();
    controller.attach(window);

    return app.exec();
}
