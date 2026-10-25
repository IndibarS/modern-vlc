/*****************************************************************************
 * Copyright (C) 2020 VLC authors and VideoLAN
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * ( at your option ) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/
#include "systempalette.hpp"
#include <QGuiApplication>
#include <QPalette>
#include <QSettings>
#include <QFile>
#include <maininterface/mainctx.hpp>
#include <vlc_modules.h>
#include <vlc_picture.h>

namespace {

QColor setColorAlpha(const QColor& c1, float alpha)
{
    QColor c(c1);
    c.setAlphaF(alpha);
    return c;
}

#ifndef _WIN32
/**
 * function taken from QtBase gui/platform/unix/qgenericunixservices.cpp
 */
static inline QByteArray detectLinuxDesktopEnvironment()
{
    const QByteArray xdgCurrentDesktop = qgetenv("XDG_CURRENT_DESKTOP");
    if (!xdgCurrentDesktop.isEmpty())
        return xdgCurrentDesktop.toUpper(); // KDE, GNOME, UNITY, LXDE, MATE, XFCE...

    // Classic fallbacks
    if (!qEnvironmentVariableIsEmpty("KDE_FULL_SESSION"))
        return QByteArrayLiteral("KDE");
    if (!qEnvironmentVariableIsEmpty("GNOME_DESKTOP_SESSION_ID"))
        return QByteArrayLiteral("GNOME");

    // Fallback to checking $DESKTOP_SESSION (unreliable)
    QByteArray desktopSession = qgetenv("DESKTOP_SESSION");

    // This can be a path in /usr/share/xsessions
    int slash = desktopSession.lastIndexOf('/');
    if (slash != -1) {
        QSettings desktopFile(QFile::decodeName(desktopSession + ".desktop"), QSettings::IniFormat);
        desktopFile.beginGroup(QStringLiteral("Desktop Entry"));
        QByteArray desktopName = desktopFile.value(QStringLiteral("DesktopNames")).toByteArray();
        if (!desktopName.isEmpty())
            return desktopName;

        // try decoding just the basename
        desktopSession = desktopSession.mid(slash + 1);
    }

    if (desktopSession == "gnome")
        return QByteArrayLiteral("GNOME");
    else if (desktopSession == "xfce")
        return QByteArrayLiteral("XFCE");
    else if (desktopSession == "kde")
        return QByteArrayLiteral("KDE");

    return QByteArrayLiteral("UNKNOWN");
}

bool isGTKBasedEnvironment()
{
    QList<QByteArray> gtkBasedEnvironments{
        "GNOME",
        "X-CINNAMON",
        "UNITY",
        "MATE",
        "XFCE",
        "LXDE"
    };
    const QList<QByteArray> desktopNames = detectLinuxDesktopEnvironment().split(':');
    for (const QByteArray& desktopName: desktopNames)
    {
        if (gtkBasedEnvironments.contains(desktopName))
            return true;
    }
    return false;
}
#endif


static void PaletteChangedCallback(vlc_qt_theme_provider_t*, void* data)
{
    auto priv = static_cast<ExternalPaletteImpl*>(data);
    emit priv->paletteChanged();
}

static void MetricsChangedCallback(vlc_qt_theme_provider_t*, vlc_qt_theme_image_type type, void* data)
{
    auto priv = static_cast<ExternalPaletteImpl*>(data);
    assert(priv);
    priv->updateMetrics(type);
}


static void ReleaseVLCPictureCb(void* data)
{
    auto pic = static_cast<picture_t*>(data);
    if (pic)
        picture_Release(pic);
}


static void setColorRBGAInt(
    vlc_qt_theme_provider_t* obj,
    vlc_qt_theme_color_set set, vlc_qt_theme_color_section section,
    vlc_qt_theme_color_name name, vlc_qt_theme_color_state state,
    int r, int g, int b, int a)
{
    auto palette = static_cast<SystemPalette*>(obj->setColorData);
    QColor color(r,g,b,a);
    palette->setColor(
        static_cast<ColorContext::ColorSet>(set), static_cast<ColorContext::ColorSection>(section),
        static_cast<ColorContext::ColorName>(name), static_cast<ColorContext::ColorState>(state),
        color);
}

static void setColorRBGAFloat(
    vlc_qt_theme_provider_t* obj,
    vlc_qt_theme_color_set set, vlc_qt_theme_color_section section,
    vlc_qt_theme_color_name name, vlc_qt_theme_color_state state,
    double r, double g, double b, double a)
{
    auto palette = static_cast<SystemPalette*>(obj->setColorData);
    QColor color;
    color.setRgbF(r,g,b,a);
    palette->setColor(
        static_cast<ColorContext::ColorSet>(set), static_cast<ColorContext::ColorSection>(section),
        static_cast<ColorContext::ColorName>(name), static_cast<ColorContext::ColorState>(state),
        color);
}

}

//ExternalPaletteImpl

ExternalPaletteImpl::ExternalPaletteImpl(MainCtx* ctx, SystemPalette& palette, QObject* parent)
    : QObject(parent)
    , m_palette(palette)
    , m_ctx(ctx)
{
}

ExternalPaletteImpl::~ExternalPaletteImpl()
{
    if (m_provider)
    {
        if (m_provider->close)
            m_provider->close(m_provider);
        if (m_module)
            module_unneed(m_provider, m_module);
        vlc_object_delete(m_provider);
    }
}

bool ExternalPaletteImpl::init()
{
    QString preferedProvider;
#ifndef _WIN32
    if (isGTKBasedEnvironment())
        preferedProvider = "qt-themeprovider-gtk";
#endif

    m_provider = vlc_object_create<vlc_qt_theme_provider_t>(m_ctx->getIntf());
    if (!m_provider)
        return false;


    m_provider->paletteUpdated = PaletteChangedCallback;
    m_provider->paletteUpdatedData = this;

    m_provider->metricsUpdated = MetricsChangedCallback;
    m_provider->metricsUpdatedData = this;

    m_provider->setColorF = setColorRBGAFloat;
    m_provider->setColorInt = setColorRBGAInt;
    m_provider->setColorData = &m_palette;

    m_module = module_need(m_provider, "qt theme provider",
                           preferedProvider.isNull() ? nullptr : qtu(preferedProvider),
                           true);
    if (!m_module)
        return false;
    return true;
}

bool ExternalPaletteImpl::isThemeDark() const
{
    if (!m_provider->isThemeDark)
        return false;
    return m_provider->isThemeDark(m_provider);
}

bool ExternalPaletteImpl::hasCSDImages() const
{
    if (!m_provider->supportThemeImage)
        return false;
    return m_provider->supportThemeImage(m_provider, VLC_QT_THEME_IMAGE_TYPE_CSD_BUTTON);
}


QImage ExternalPaletteImpl::getCSDImage(vlc_qt_theme_csd_button_type type, vlc_qt_theme_csd_button_state state, bool maximized, bool active, int bannerHeight)
{
    if (!m_provider->getThemeImage)
        return {};
    vlc_qt_theme_image_setting imageSettings;
    imageSettings.windowScaleFactor = m_ctx->intfMainWindow()->devicePixelRatio();
    imageSettings.userScaleFacor = m_ctx->getIntfUserScaleFactor();
    imageSettings.u.csdButton.buttonType = type;
    imageSettings.u.csdButton.state = state;
    imageSettings.u.csdButton.maximized = maximized;
    imageSettings.u.csdButton.active = active;
    imageSettings.u.csdButton.bannerHeight = bannerHeight;
    picture_t* pic = m_provider->getThemeImage(m_provider, VLC_QT_THEME_IMAGE_TYPE_CSD_BUTTON, &imageSettings);
    if (!pic)
        return {};

    QImage::Format format = QImage::Format_Invalid;
    switch (pic->format.i_chroma)
    {
    case VLC_CODEC_ARGB:
        format = QImage::Format_ARGB32_Premultiplied;
        break;
    case VLC_CODEC_RGBA:
        format = QImage::Format_RGBA8888_Premultiplied;
        break;
    case VLC_CODEC_RGB24:
        format = QImage::Format_RGB888;
        break;
    default:
        msg_Err(m_ctx->getIntf(), "unexpected image format from theme provider");
        break;
    }

    return QImage(pic->p[0].p_pixels,
            pic->format.i_visible_width, pic->format.i_visible_height, pic->p[0].i_pitch,
            format,
            ReleaseVLCPictureCb, pic
            );
}

CSDMetrics* ExternalPaletteImpl::getCSDMetrics() const
{
    return m_csdMetrics.get();
}

int ExternalPaletteImpl::update()
{
    if (m_provider->updatePalette)
        return m_provider->updatePalette(m_provider);
    return VLC_EGENERIC;
}

void ExternalPaletteImpl::updateMetrics(vlc_qt_theme_image_type type)
{
    if (m_provider->getThemeMetrics)
    {
        if (type == VLC_QT_THEME_IMAGE_TYPE_CSD_BUTTON)
        {
            m_csdMetrics.reset();
            [&](){
                if (!m_provider->getThemeMetrics)
                    return;
                vlc_qt_theme_metrics metrics;
                memset(&metrics, 0, sizeof(metrics));
                bool ret = m_provider->getThemeMetrics(m_provider, VLC_QT_THEME_IMAGE_TYPE_CSD_BUTTON, &metrics);
                if (!ret)
                    return;
                m_csdMetrics = std::make_unique<CSDMetrics>();
                m_csdMetrics->interNavButtonSpacing = metrics.u.csd.interNavButtonSpacing;

                m_csdMetrics->csdFrameMarginLeft = metrics.u.csd.csdFrameMarginLeft;
                m_csdMetrics->csdFrameMarginRight = metrics.u.csd.csdFrameMarginRight;
                m_csdMetrics->csdFrameMarginTop = metrics.u.csd.csdFrameMarginTop;
                m_csdMetrics->csdFrameMarginBottom = metrics.u.csd.csdFrameMarginBottom;
            }();
            emit CSDMetricsChanged();
        }
    }
}

SystemPalette::SystemPalette(QObject* parent)
    : QObject(parent)
{

}

void SystemPalette::componentComplete()
{
    {
        assert(m_ctx);
        char* accentColor = var_InheritString(m_ctx->getIntf(), "qt-accent-color");
        assert(accentColor);

        if (!strcmp(accentColor, "default"))
        {
            // Use default
        }
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
        else if (!strcmp(accentColor, "system"))
        {
            assert(qApp);
            const auto palette = qApp->palette();

            orange500 = palette.color(QPalette::Normal, QPalette::Accent);
            orange800 = orange500.lighter(150);
        }
#endif
        else
        {
            orange500 = QColor(accentColor);
            orange800 = orange500.lighter(150);
        }

        free(accentColor);
    }

    updatePalette();
}

ColorSchemeModel::ColorScheme SystemPalette::source() const
{
    return m_source;
}

QImage SystemPalette::getCSDImage(vlc_qt_theme_csd_button_type type, vlc_qt_theme_csd_button_state state, bool maximized, bool active, int bannerHeight)
{
    if (!hasCSDImage())
        return QImage();
    assert(m_palettePriv);
    return m_palettePriv->getCSDImage(type, state, maximized, active, bannerHeight);

}

CSDMetrics* SystemPalette::getCSDMetrics() const
{
    if (!m_palettePriv)
        return nullptr;
    return m_palettePriv->getCSDMetrics();
}

bool SystemPalette::hasCSDImage() const
{
    if (!m_palettePriv)
        return false;
    return m_palettePriv->hasCSDImages();
}

void SystemPalette::setSource(ColorSchemeModel::ColorScheme source)
{
    if (m_source == source)
        return;
    m_source = source;

    updatePalette();
    emit sourceChanged();
}

void SystemPalette::setCtx(MainCtx* ctx)
{
    if (ctx == m_ctx)
        return;
    m_ctx = ctx;
    emit ctxChanged();
    updatePalette();
}

void SystemPalette::updatePalette()
{
    m_palettePriv.reset();
    switch(m_source)
    {
    case ColorSchemeModel::System:
        makeSystemPalette();
        break;
    case ColorSchemeModel::Day:
        makeLightPalette();
        break;
    case ColorSchemeModel::Night:
        makeDarkPalette();
        break;
    default:
        break;
    }

    if (m_palettePriv)
    {
        connect(
            m_palettePriv.get(), &ExternalPaletteImpl::paletteChanged,
            this, &SystemPalette::updatePalette);
        connect(
            m_palettePriv.get(), &ExternalPaletteImpl::CSDMetricsChanged,
            this, &SystemPalette::CSDMetricsChanged);
    }
    emit paletteChanged();

    bool hasCSDImage = m_palettePriv && m_palettePriv->hasCSDImages();
    if (m_hasCSDImage != hasCSDImage)
    {
        m_hasCSDImage = hasCSDImage;
        emit hasCSDImageChanged();
    }
}

static quint64 makeKey(ColorContext::ColorSet colorSet, ColorContext::ColorSection section,
                       ColorContext::ColorName name, ColorContext::ColorState state)
{
    static_assert(VQTC_STATE_COUNT < (1<<4), "");
    static_assert(VQTC_SECTION_COUNT < (1<<4), "");
    static_assert(VQTC_NAME_COUNT < (1<<8), "");
    static_assert(VQTC_SET_COUNT < (1<<16), "");
    return  (colorSet << 16)
        + (name << 8)
        + (section << 4)
        + state;
}

void SystemPalette::setColor(ColorContext::ColorSet colorSet,  ColorContext::ColorSection section,
                             ColorContext::ColorName name, ColorContext::ColorState state, QColor color)
{

    quint64 key = makeKey(colorSet, section, name, state);
    m_colorMap[key] = color;
}


QColor SystemPalette::getColor(ColorContext::ColorSet colorSet, ColorContext::ColorSection section,
                               ColorContext::ColorName name, ColorContext::ColorState state) const
{
    typedef ColorContext C;

    quint64 key = makeKey(colorSet, section, name, state);
    auto it = m_colorMap.find(key);
    if (it != m_colorMap.cend())
        return *it;
    //we don't have the role explicitly set, fallback to the normal state
    key = makeKey(colorSet, section, name, C::Normal);
    it = m_colorMap.find(key);
    if (it != m_colorMap.cend())
        return *it;
    //we don't have the role explicitly set, fallback to the colorSet View
    //TODO do we want finer hierarchy?
    if (colorSet != C::View)
    {
        return getColor(C::View, section, name, state);
    }
    else
    {
        //nothing matches, that's probably an issue, return an ugly color
        return Qt::magenta;
    }
}


void SystemPalette::makeLightPalette()
{
    m_isDark = false;

    typedef ColorContext C;

    m_colorMap.clear();

    //base set
    {
        C::ColorSet CS = C::View;
        setColor(CS, C::Bg, C::Primary, C::Normal, lightGrey100 );
        setColor(CS, C::Bg, C::Secondary, C::Normal, Qt::white );

        setColor(CS, C::Fg, C::Primary, C::Normal, darkGrey300);
        setColor(CS, C::Fg, C::Primary, C::Disabled, setColorAlpha(Qt::black, 0.3));

        setColor(CS, C::Fg, C::Secondary, C::Normal, setColorAlpha(Qt::black, 0.7));

        setColor(CS, C::Bg, C::Negative, C::Normal, QColor("#fde7e9")); //FIXME
        setColor(CS, C::Fg, C::Negative, C::Normal, Qt::red);

        setColor(CS, C::Bg, C::Neutral, C::Normal, QColor("#e4dab8")); //FIXME
        setColor(CS, C::Fg, C::Neutral, C::Normal, orange800);

        setColor(CS, C::Bg, C::Positive, C::Normal, QColor("#dff6dd")); //FIXME
        setColor(CS, C::Fg, C::Positive, C::Normal, Qt::green);

        setColor(CS, C::Decoration, C::VisualFocus, C::Normal, setColorAlpha(Qt::black, 0.0) );
        setColor(CS, C::Decoration, C::VisualFocus, C::Focused, Qt::black );

        setColor(CS, C::Decoration, C::Border, C::Normal, setColorAlpha(Qt::black, 0.4) );
        setColor(CS, C::Decoration, C::Border, C::Focused, setColorAlpha(Qt::black, 0.7) );
        setColor(CS, C::Decoration, C::Border, C::Hovered, setColorAlpha(Qt::black, 0.7) );
        setColor(CS, C::Decoration, C::Border, C::Disabled, setColorAlpha(Qt::black, 0.0) );

        setColor(CS, C::Decoration, C::Separator, C::Normal, QColor("#E0E0E0")); //FIXME not a predef

        setColor(CS, C::Decoration, C::Shadow, C::Normal, setColorAlpha(Qt::black, 0.22));

        setColor(CS, C::Decoration, C::Accent, C::Normal, orange800);
        setColor(CS, C::Fg, C::Link, C::Normal, orange800 /* accent */);
    }

    //window banner & miniplayer
    {
        C::ColorSet CS = C::Window;
        setColor(CS, C::Bg, C::Primary, C::Normal, lightGrey400); // Off-white surface, approximately #FDFDFD.
        setColor(CS, C::Bg, C::Secondary, C::Normal, lightGrey400);
        setColor(CS, C::Decoration, C::Border, C::Normal, QColor{"#E0E0E0"}); //FIXME not a predef
    }

    //badges
    {
        C::ColorSet CS = C::Badge;
        setColor(CS, C::Bg, C::Primary, C::Normal, setColorAlpha(Qt::black, 0.6));
        setColor(CS, C::Fg, C::Primary, C::Normal, Qt::white);
    }

    //tab button
    {
        C::ColorSet CS = C::TabButton;
        setColor(CS, C::Bg, C::Primary, C::Normal, setColorAlpha(lightGrey300, 0.0));
        setColor(CS, C::Bg, C::Primary, C::Focused, lightGrey300);
        setColor(CS, C::Bg, C::Primary, C::Hovered, lightGrey300);

        setColor(CS, C::Fg, C::Primary, C::Normal, setColorAlpha(Qt::black, 0.6));
        setColor(CS, C::Fg, C::Primary, C::Focused, Qt::black);
        setColor(CS, C::Fg, C::Primary, C::Hovered, Qt::black);
        setColor(CS, C::Fg, C::Primary, C::Disabled, setColorAlpha(Qt::black, 0.2));
        setColor(CS, C::Fg, C::Secondary, C::Normal, Qt::black);
    }

    //tool button
    {
        C::ColorSet CS = C::ToolButton;
        setColor(CS, C::Bg, C::Primary, C::Normal, Qt::transparent);
        setColor(CS, C::Bg, C::Secondary, C::Normal, lightGrey400);

        setColor(CS, C::Fg, C::Primary, C::Normal, setColorAlpha(Qt::black, 0.6));
        setColor(CS, C::Fg, C::Primary, C::Focused, Qt::black);
        setColor(CS, C::Fg, C::Primary, C::Hovered, Qt::black);
        setColor(CS, C::Fg, C::Primary, C::Disabled, setColorAlpha(Qt::black, 0.2));
        setColor(CS, C::Fg, C::Secondary, C::Normal, Qt::black);

        setColor(CS, C::Decoration, C::Border, C::Normal, Qt::transparent);
        setColor(CS, C::Decoration, C::Border, C::Focused, Qt::transparent);
        setColor(CS, C::Decoration, C::Border, C::Hovered, Qt::transparent);
        setColor(CS, C::Decoration, C::Border, C::Disabled, Qt::transparent);
    }

    //menubar
    {
        C::ColorSet CS = C::MenuBar;
        setColor(CS, C::Bg, C::Primary, C::Normal, setColorAlpha(lightGrey700, 0.0));
        setColor(CS, C::Bg, C::Primary, C::Focused, lightGrey700);
        setColor(CS, C::Bg, C::Primary, C::Hovered, lightGrey700);
        setColor(CS, C::Fg, C::Primary, C::Normal, Qt::black);
        setColor(CS, C::Fg, C::Primary, C::Disabled, setColorAlpha(Qt::black, 0.2));
    }

    //Item
    {
        C::ColorSet CS = C::Item;
        setColor(CS, C::Bg, C::Primary, C::Normal, setColorAlpha(lightGrey600, 0.0));
        setColor(CS, C::Bg, C::Primary, C::Focused, setColorAlpha(lightGrey600, 0.5));
        setColor(CS, C::Bg, C::Primary, C::Hovered, setColorAlpha(lightGrey600, 0.5));

        setColor(CS, C::Bg, C::Highlight, C::Normal, lightGrey600);
        setColor(CS, C::Bg, C::Highlight, C::Focused, setColorAlpha(lightGrey600, 0.8));
        setColor(CS, C::Bg, C::Highlight, C::Hovered, setColorAlpha(lightGrey600, 0.8));
        setColor(CS, C::Fg, C::Highlight, C::Normal, Qt::black);

        setColor(CS, C::Fg, C::Primary, C::Normal, Qt::black);
        setColor(CS, C::Fg, C::Secondary, C::Normal, setColorAlpha(Qt::black, 0.6));

        setColor(CS, C::Decoration, C::Indicator, C::Normal, QColor("#9e9e9e")); //FIXME not a predef

    }

    //Accent Buttons
    {
        C::ColorSet CS = C::ButtonAccent;
        setColor(CS, C::Bg, C::Primary, C::Normal, orange800);
        setColor(CS, C::Bg, C::Primary, C::Pressed, QColor("#e65609"));  //FIXME not a predef
        setColor(CS, C::Bg, C::Primary, C::Disabled, setColorAlpha(Qt::black, 0.2));

        setColor(CS, C::Fg, C::Primary, C::Normal, Qt::white);
        setColor(CS, C::Fg, C::Primary, C::Disabled, setColorAlpha(Qt::black, 0.3));

        setColor(CS, C::Decoration, C::Border, C::Normal, Qt::transparent);
        setColor(CS, C::Decoration, C::Border, C::Focused, Qt::transparent);
        setColor(CS, C::Decoration, C::Border, C::Hovered, Qt::transparent);
        setColor(CS, C::Decoration, C::Border, C::Disabled, Qt::transparent);
    }

    //Standard Buttons
    {
        C::ColorSet CS = C::ButtonStandard;
        setColor(CS, C::Bg, C::Primary, C::Normal, Qt::transparent);

        setColor(CS, C::Fg, C::Primary, C::Normal, setColorAlpha(Qt::black, 0.6));
        setColor(CS, C::Fg, C::Primary, C::Focused, Qt::black);
        setColor(CS, C::Fg, C::Primary, C::Hovered, Qt::black);
        setColor(CS, C::Fg, C::Primary, C::Disabled, setColorAlpha(Qt::black, 0.3));

        setColor(CS, C::Decoration, C::Border, C::Normal, Qt::transparent);
        setColor(CS, C::Decoration, C::Border, C::Focused, Qt::transparent);
        setColor(CS, C::Decoration, C::Border, C::Hovered, Qt::transparent);
        setColor(CS, C::Decoration, C::Border, C::Disabled, Qt::transparent);
    }

    //tooltip
    {
        C::ColorSet CS = C::Tooltip;
        setColor(CS, C::Bg, C::Primary, C::Normal, lightGrey200);
        setColor(CS, C::Fg, C::Primary, C::Normal, Qt::black);
    }

    //slider
    {
        C::ColorSet CS = C::Slider;
        setColor(CS, C::Bg, C::Primary, C::Normal, lightGrey400); //#EEEEEE on the designs
        setColor(CS, C::Bg, C::Secondary, C::Normal, setColorAlpha("#lightGrey400", 0.2));
        setColor(CS, C::Fg, C::Primary, C::Normal, orange800);
        setColor(CS, C::Fg, C::Positive, C::Normal, "#0F7B0F");  //FIXME
        setColor(CS, C::Fg, C::Neutral, C::Normal, "#9D5D00");  //FIXME
        setColor(CS, C::Fg, C::Negative, C::Normal, "#C42B1C");  //FIXME
    }

    //Combo box
    {
        C::ColorSet CS = C::ComboBox;
        setColor(CS, C::Fg, C::Primary, C::Normal, Qt::black);
        setColor(CS, C::Bg, C::Primary, C::Normal, setColorAlpha(Qt::white, 0.8));
        setColor(CS, C::Bg, C::Secondary, C::Normal, lightGrey500);
    }

    //TextField
    {
        C::ColorSet CS = C::TextField;
        setColor(CS, C::Decoration, C::Border, C::Normal, setColorAlpha(Qt::black, 0.4) );
        setColor(CS, C::Decoration, C::Border, C::Focused, orange800);
        setColor(CS, C::Decoration, C::Border, C::Hovered, setColorAlpha(Qt::black, 0.7) );
        setColor(CS, C::Decoration, C::Border, C::Disabled, setColorAlpha(Qt::black, 0.0) );

        setColor(CS, C::Bg, C::Highlight, C::Normal, darkGrey800); //FIXME
        setColor(CS, C::Fg, C::Highlight, C::Normal, Qt::white); //FIXME
    }

    //Switch
    {
        C::ColorSet CS = C::Switch;
        setColor(CS, C::Bg, C::Primary, C::Normal, setColorAlpha(Qt::black, 0.05));
        setColor(CS, C::Fg, C::Primary, C::Normal, setColorAlpha(Qt::black, 0.55));
        setColor(CS, C::Decoration, C::Border, C::Normal, setColorAlpha(Qt::black, 0.55));

        setColor(CS, C::Bg, C::Secondary, C::Normal, orange800);
        setColor(CS, C::Fg, C::Secondary, C::Normal, Qt::white);
    }

    //SpinBox
    {
        C::ColorSet CS = C::SpinBox;
        setColor(CS, C::Decoration, C::Border, C::Normal, setColorAlpha(Qt::black, 0.4) );
        setColor(CS, C::Decoration, C::Border, C::Focused, orange800);
        setColor(CS, C::Decoration, C::Border, C::Hovered, setColorAlpha(Qt::black, 0.7) );
        setColor(CS, C::Decoration, C::Border, C::Disabled, setColorAlpha(Qt::black, 0.0) );

        setColor(CS, C::Bg, C::Highlight, C::Normal, darkGrey800); //FIXME
        setColor(CS, C::Fg, C::Highlight, C::Normal, Qt::white); //FIXME
    }
    applyRedesignPalette();
}

void SystemPalette::makeDarkPalette()
{
    m_isDark = true;

    m_colorMap.clear();

    typedef ColorContext C;

    {
        C::ColorSet CS = C::View;
        setColor(CS, C::Bg, C::Primary, C::Normal, darkGrey300 );
        setColor(CS, C::Bg, C::Secondary, C::Normal, Qt::black );

        setColor(CS, C::Fg, C::Primary, C::Normal, Qt::white );
        setColor(CS, C::Fg, C::Primary, C::Disabled, setColorAlpha(Qt::white, 0.3) );

        setColor(CS, C::Fg, C::Secondary, C::Normal, setColorAlpha(Qt::white, 0.6));

        setColor(CS, C::Bg, C::Negative, C::Normal, QColor(Qt::darkRed));
        setColor(CS, C::Fg, C::Negative, C::Normal, Qt::red);

        setColor(CS, C::Bg, C::Neutral, C::Normal, QColor(Qt::darkYellow));
        setColor(CS, C::Fg, C::Neutral, C::Normal, orange500);

        setColor(CS, C::Bg, C::Positive, C::Normal, QColor(Qt::darkGreen));
        setColor(CS, C::Fg, C::Positive, C::Normal, Qt::green);

        setColor(CS, C::Decoration, C::VisualFocus, C::Normal, setColorAlpha(Qt::white, 0.0) );
        setColor(CS, C::Decoration, C::VisualFocus, C::Focused, Qt::white );

        setColor(CS, C::Decoration, C::Border, C::Normal, setColorAlpha(Qt::white, 0.4) );
        setColor(CS, C::Decoration, C::Border, C::Focused, setColorAlpha(Qt::white, 0.7) );
        setColor(CS, C::Decoration, C::Border, C::Hovered, setColorAlpha(Qt::white, 0.7) );
        setColor(CS, C::Decoration, C::Border, C::Disabled, setColorAlpha(Qt::white, 0.0) );

        setColor(CS, C::Decoration, C::Shadow, C::Normal, setColorAlpha(Qt::black, 0.22));

        setColor(CS, C::Decoration, C::Separator, C::Normal, darkGrey800);

        setColor(CS, C::Decoration, C::Accent, C::Normal, orange500);
        setColor(CS, C::Fg, C::Link, C::Normal, orange500);
    }

    //window banner & miniplayer
    {
        C::ColorSet CS = C::Window;
        setColor(CS, C::Bg, C::Primary, C::Normal, Qt::black); //FIXME
        setColor(CS, C::Bg, C::Secondary, C::Normal, Qt::black);
        setColor(CS, C::Decoration, C::Border, C::Normal, darkGrey800); //FIXME not a predef
    }

    //badges
    {
        C::ColorSet CS = C::Badge;
        setColor(CS, C::Bg, C::Primary, C::Normal, setColorAlpha(Qt::white, 0.8));
        setColor(CS, C::Fg, C::Primary, C::Normal, Qt::black);
    }

    //tab button
    {
        C::ColorSet CS = C::TabButton;
        setColor(CS, C::Bg, C::Primary, C::Normal, setColorAlpha(darkGrey800, 0.0));
        setColor(CS, C::Bg, C::Primary, C::Focused, darkGrey800);
        setColor(CS, C::Bg, C::Primary, C::Hovered, darkGrey800);

        setColor(CS, C::Fg, C::Primary, C::Normal, setColorAlpha(Qt::white, 0.6));
        setColor(CS, C::Fg, C::Primary, C::Focused, Qt::white);
        setColor(CS, C::Fg, C::Primary, C::Hovered, Qt::white);
        setColor(CS, C::Fg, C::Primary, C::Disabled, setColorAlpha(Qt::white, 0.2));
        setColor(CS, C::Fg, C::Secondary, C::Normal, Qt::white);
    }

    //tool button
    {
        C::ColorSet CS = C::ToolButton;
        setColor(CS, C::Bg, C::Primary, C::Normal, Qt::transparent);
        setColor(CS, C::Bg, C::Secondary, C::Normal, Qt::black);

        setColor(CS, C::Fg, C::Primary, C::Normal, setColorAlpha(Qt::white, 0.6));
        setColor(CS, C::Fg, C::Primary, C::Focused, Qt::white);
        setColor(CS, C::Fg, C::Primary, C::Hovered, Qt::white);
        setColor(CS, C::Fg, C::Primary, C::Disabled, setColorAlpha(Qt::white, 0.2));
        setColor(CS, C::Fg, C::Secondary, C::Normal, Qt::white);

        setColor(CS, C::Decoration, C::Border, C::Normal, Qt::transparent);
        setColor(CS, C::Decoration, C::Border, C::Focused, Qt::transparent);
        setColor(CS, C::Decoration, C::Border, C::Hovered, Qt::transparent);
        setColor(CS, C::Decoration, C::Border, C::Disabled, Qt::transparent);
    }

    //menubar
    {
        C::ColorSet CS = C::MenuBar;
        setColor(CS, C::Bg, C::Primary, C::Normal, setColorAlpha(darkGrey800, 0.0));
        setColor(CS, C::Bg, C::Primary, C::Focused, darkGrey800);
        setColor(CS, C::Bg, C::Primary, C::Hovered, darkGrey800);
        setColor(CS, C::Fg, C::Primary, C::Normal, Qt::white);
        setColor(CS, C::Fg, C::Primary, C::Disabled, setColorAlpha(Qt::white, 0.2));
    }

    //Item
    {
        C::ColorSet CS = C::Item;
        setColor(CS, C::Bg, C::Primary, C::Normal, setColorAlpha(darkGrey800, 0.0));
        setColor(CS, C::Bg, C::Primary, C::Focused, setColorAlpha(darkGrey800, 0.5));
        setColor(CS, C::Bg, C::Primary, C::Hovered, setColorAlpha(darkGrey800, 0.5));

        setColor(CS, C::Bg, C::Highlight, C::Normal, darkGrey800);
        setColor(CS, C::Bg, C::Highlight, C::Focused, setColorAlpha(darkGrey800, 0.8));
        setColor(CS, C::Bg, C::Highlight, C::Hovered, setColorAlpha(darkGrey800, 0.8));
        setColor(CS, C::Fg, C::Highlight, C::Normal, Qt::white);

        setColor(CS, C::Fg, C::Primary, C::Normal, Qt::white);
        setColor(CS, C::Fg, C::Secondary, C::Normal, setColorAlpha(Qt::white, 0.6));

        setColor(CS, C::Decoration, C::Indicator, C::Normal, QColor("#666666"));  //FIXME not a predef
    }

    //Accent Buttons
    {
        C::ColorSet CS = C::ButtonAccent;
        setColor(CS, C::Bg, C::Primary, C::Normal, orange500);
        setColor(CS, C::Bg, C::Primary, C::Pressed, QColor("#e67a30"));  //FIXME not a predef
        setColor(CS, C::Bg, C::Primary, C::Disabled, setColorAlpha(Qt::white, 0.2));

        setColor(CS, C::Fg, C::Primary, C::Normal, Qt::white);
        setColor(CS, C::Fg, C::Primary, C::Disabled, setColorAlpha(Qt::white, 0.3));

        setColor(CS, C::Decoration, C::Border, C::Normal, Qt::transparent);
        setColor(CS, C::Decoration, C::Border, C::Focused, Qt::transparent);
        setColor(CS, C::Decoration, C::Border, C::Hovered, Qt::transparent);
        setColor(CS, C::Decoration, C::Border, C::Disabled, Qt::transparent);
    }

    //Standard Buttons
    {
        C::ColorSet CS = C::ButtonStandard;
        setColor(CS, C::Bg, C::Primary, C::Normal, Qt::transparent);

        setColor(CS, C::Fg, C::Primary, C::Normal, setColorAlpha(Qt::white, 0.6));
        setColor(CS, C::Fg, C::Primary, C::Focused, Qt::white);
        setColor(CS, C::Fg, C::Primary, C::Hovered, Qt::white);
        setColor(CS, C::Fg, C::Primary, C::Disabled, setColorAlpha(Qt::white, 0.3));

        setColor(CS, C::Decoration, C::Border, C::Normal, Qt::transparent);
        setColor(CS, C::Decoration, C::Border, C::Focused, Qt::transparent);
        setColor(CS, C::Decoration, C::Border, C::Hovered, Qt::transparent);
        setColor(CS, C::Decoration, C::Border, C::Disabled, Qt::transparent);
    }

    //tooltip
    {
        C::ColorSet CS = C::Tooltip;
        setColor(CS, C::Bg, C::Primary, C::Normal, darkGrey200);
        setColor(CS, C::Fg, C::Primary, C::Normal, Qt::white);
    }

    //slider
    {
        C::ColorSet CS = C::Slider;
        setColor(CS, C::Bg, C::Primary, C::Normal, setColorAlpha("#929292", 0.2)); //FIXME not in the palette
        setColor(CS, C::Bg, C::Primary, C::Focused, setColorAlpha("#929292", 0.4));
        setColor(CS, C::Bg, C::Primary, C::Hovered, setColorAlpha("#929292", 0.4));

        setColor(CS, C::Fg, C::Primary, C::Normal, orange500);
        setColor(CS, C::Fg, C::Positive, C::Normal, "#0F7B0F");  //FIXME
        setColor(CS, C::Fg, C::Neutral, C::Normal, "#9D5D00");  //FIXME
        setColor(CS, C::Fg, C::Negative, C::Normal, "#C42B1C");  //FIXME
    }

    //Combo box
    {
        C::ColorSet CS = C::ComboBox;
        setColor(CS, C::Fg, C::Primary, C::Normal, Qt::white);
        setColor(CS, C::Bg, C::Primary, C::Normal, setColorAlpha(darkGrey300, 0.8));
        setColor(CS, C::Bg, C::Secondary, C::Normal, darkGrey500);
    }

    //TextField
    {
        C::ColorSet CS = C::TextField;
        setColor(CS, C::Decoration, C::Border, C::Normal, setColorAlpha(Qt::white, 0.4) );
        setColor(CS, C::Decoration, C::Border, C::Focused, orange500 );
        setColor(CS, C::Decoration, C::Border, C::Hovered, setColorAlpha(Qt::white, 0.7) );
        setColor(CS, C::Decoration, C::Border, C::Disabled, setColorAlpha(Qt::white, 0.0) );

        setColor(CS, C::Bg, C::Highlight, C::Normal, lightGrey600); //FIXME
        setColor(CS, C::Fg, C::Highlight, C::Normal, Qt::black); //FIXME
    }

    //Switch
    {
        C::ColorSet CS = C::Switch;
        setColor(CS, C::Bg, C::Primary, C::Normal, setColorAlpha(Qt::white, 0.05));
        setColor(CS, C::Fg, C::Primary, C::Normal, setColorAlpha(Qt::white, 0.55));
        setColor(CS, C::Decoration, C::Border, C::Normal, setColorAlpha(Qt::white, 0.55));
        setColor(CS, C::Bg, C::Secondary, C::Normal, orange500);
        setColor(CS, C::Fg, C::Secondary, C::Normal, Qt::black);
    }

    //Spinbox
    {
        C::ColorSet CS = C::SpinBox;
        setColor(CS, C::Decoration, C::Border, C::Normal, setColorAlpha(Qt::white, 0.4) );
        setColor(CS, C::Decoration, C::Border, C::Focused, orange500 );
        setColor(CS, C::Decoration, C::Border, C::Hovered, setColorAlpha(Qt::white, 0.7) );
        setColor(CS, C::Decoration, C::Border, C::Disabled, setColorAlpha(Qt::white, 0.0) );

        setColor(CS, C::Bg, C::Highlight, C::Normal, lightGrey600); //FIXME
        setColor(CS, C::Fg, C::Highlight, C::Normal, Qt::black); //FIXME
    }
    applyRedesignPalette();
}

// Shared roles for the dark/light player and settings surfaces.
// Keep state-specific focus/error colors from the base palettes.
void SystemPalette::applyRedesignPalette()
{
    using C = ColorContext;
    const QColor surface(m_isDark ? "#1b1b1b" : "#d5d5d5");
    const QColor background(m_isDark ? "#0e0e0e" : "#e7e7e7");
    const QColor field(m_isDark ? "#090909" : "#dedede");
    const QColor raised(m_isDark ? "#191919" : "#dedede");
    const QColor foreground(m_isDark ? "#969696" : "#2e2e2e");
    const QColor secondary(m_isDark ? "#8f8f8f" : "#2e2e2e");
    const QColor divider(m_isDark ? "#373737" : "#9b9b9b");
    const QColor accent = m_isDark ? orange500 : orange800;

    setColor(C::View, C::Bg, C::Primary, C::Normal, background);
    setColor(C::View, C::Bg, C::Secondary, C::Normal, field);
    setColor(C::View, C::Fg, C::Primary, C::Normal, foreground);
    setColor(C::View, C::Fg, C::Secondary, C::Normal, secondary);
    setColor(C::View, C::Decoration, C::Separator, C::Normal, divider);
    setColor(C::View, C::Decoration, C::Accent, C::Normal, accent);
    setColor(C::Window, C::Bg, C::Primary, C::Normal, surface);
    setColor(C::Window, C::Bg, C::Secondary, C::Normal, raised);
    for (auto set : {C::ToolButton, C::TabButton, C::MenuBar, C::Item})
        setColor(set, C::Fg, C::Primary, C::Normal, secondary);
    setColor(C::Item, C::Fg, C::Highlight, C::Normal, accent);
    setColor(C::ButtonStandard, C::Bg, C::Primary, C::Normal, raised);
    setColor(C::ButtonStandard, C::Fg, C::Primary, C::Normal, foreground);
    setColor(C::ButtonAccent, C::Fg, C::Primary, C::Normal, QColor("#161616"));
    setColor(C::ButtonAccent, C::Bg, C::Primary, C::Normal, accent);
    setColor(C::ComboBox, C::Bg, C::Primary, C::Normal, raised);
    setColor(C::ComboBox, C::Fg, C::Primary, C::Normal, secondary);
    setColor(C::TextField, C::Bg, C::Primary, C::Normal, field);
    setColor(C::TextField, C::Decoration, C::Border, C::Normal, raised);
    setColor(C::Slider, C::Bg, C::Primary, C::Normal, field);
    setColor(C::Slider, C::Fg, C::Primary, C::Normal, accent);
}

// Translate the same semantic colors used by QML for the legacy Qt Widgets
// dialogs. This keeps native controls and their behavior while sharing a theme.
QPalette SystemPalette::widgetPalette() const
{
    using C = ColorContext;
    QPalette result;
    const auto role = [this, &result](QPalette::ColorRole role, C::ColorSet set,
                                     C::ColorSection section, C::ColorName name) {
        result.setColor(QPalette::Active, role, getColor(set, section, name, C::Normal));
        result.setColor(QPalette::Inactive, role, getColor(set, section, name, C::Normal));
        result.setColor(QPalette::Disabled, role, getColor(set, section, name, C::Disabled));
    };
    role(QPalette::Window, C::Window, C::Bg, C::Primary);
    role(QPalette::WindowText, C::View, C::Fg, C::Primary);
    role(QPalette::Base, C::TextField, C::Bg, C::Primary);
    role(QPalette::AlternateBase, C::Window, C::Bg, C::Secondary);
    role(QPalette::Text, C::View, C::Fg, C::Primary);
    role(QPalette::Button, C::ButtonStandard, C::Bg, C::Primary);
    role(QPalette::ButtonText, C::ButtonStandard, C::Fg, C::Primary);
    role(QPalette::Highlight, C::ButtonAccent, C::Bg, C::Primary);
    role(QPalette::HighlightedText, C::ButtonAccent, C::Fg, C::Primary);
    role(QPalette::ToolTipBase, C::Window, C::Bg, C::Secondary);
    role(QPalette::ToolTipText, C::View, C::Fg, C::Primary);
    role(QPalette::PlaceholderText, C::View, C::Fg, C::Secondary);
    role(QPalette::Link, C::View, C::Fg, C::Link);
    role(QPalette::LinkVisited, C::View, C::Fg, C::Link);
    role(QPalette::Mid, C::View, C::Decoration, C::Separator);
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
    role(QPalette::Accent, C::View, C::Decoration, C::Accent);
#endif
    result.setColor(QPalette::Light, result.color(QPalette::Window).lighter(130));
    result.setColor(QPalette::Midlight, result.color(QPalette::Window).lighter(115));
    result.setColor(QPalette::Dark, result.color(QPalette::Window).darker(140));
    result.setColor(QPalette::Shadow, Qt::black);
    return result;
}

// Palette roles keep every native dialog in step with live theme changes.
QString SystemPalette::widgetStyleSheet()
{
    return QStringLiteral(R"(
QDialog, QWizard { background: palette(window); color: palette(window-text); }
QPushButton, QToolButton {
    background: palette(button); color: palette(button-text);
    border: 1px solid palette(mid); border-radius: 6px; padding: 6px 10px;
}
QPushButton:hover, QToolButton:hover { border-color: palette(highlight); }
QPushButton:pressed, QToolButton:checked { background: palette(alternate-base); }
QPushButton:default, QPushButton:focus { border-color: palette(highlight); }
QPushButton:disabled, QToolButton:disabled { color: palette(mid); }
QLineEdit, QTextEdit, QPlainTextEdit, QAbstractSpinBox, QComboBox {
    background: palette(base); color: palette(text); border: 1px solid palette(mid);
    border-radius: 5px; padding: 5px; selection-background-color: palette(highlight);
    selection-color: palette(highlighted-text);
}
QLineEdit:focus, QTextEdit:focus, QPlainTextEdit:focus, QAbstractSpinBox:focus,
QComboBox:focus { border-color: palette(highlight); }
QMenu, QComboBox QAbstractItemView {
    background: palette(window); color: palette(window-text);
    border: 1px solid palette(mid); padding: 5px;
}
QMenu::item { padding: 6px 24px; border-radius: 4px; }
QMenu::item:selected { background: palette(highlight); color: palette(highlighted-text); }
QMenu::item:disabled { color: palette(mid); }
QMenu::separator { height: 1px; background: palette(mid); margin: 5px 8px; }
QMenuBar { background: palette(window); color: palette(window-text); }
QMenuBar::item:selected { background: palette(button); border-radius: 4px; }
QToolTip { background: palette(tool-tip-base); color: palette(tool-tip-text);
    border: 1px solid palette(mid); border-radius: 5px; padding: 6px; }
QGroupBox { border: 1px solid palette(mid); border-radius: 7px;
    margin-top: 12px; padding-top: 10px; }
QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; }
QTabWidget::pane { border: 1px solid palette(mid); border-radius: 6px; }
QTabBar::tab { background: palette(button); color: palette(button-text);
    padding: 7px 12px; border-bottom: 2px solid transparent; }
QTabBar::tab:selected { border-bottom-color: palette(highlight); background: palette(window); }
QAbstractItemView { background: palette(base); alternate-background-color: palette(alternate-base);
    color: palette(text); border: 1px solid palette(mid); border-radius: 5px;
    selection-background-color: palette(highlight); selection-color: palette(highlighted-text); }
QHeaderView::section { background: palette(button); color: palette(button-text);
    border: none; border-bottom: 1px solid palette(mid); padding: 6px; }
QProgressBar { border: 1px solid palette(mid); border-radius: 5px;
    background: palette(base); text-align: center; }
QProgressBar::chunk { background: palette(highlight); border-radius: 4px; }
)");
}

void SystemPalette::makeSystemPalette()
{
    if (!m_ctx)
    {
        //can't initialise system palette, fallback to default
        makeLightPalette();
        return;
    }

    auto palette = std::make_unique<ExternalPaletteImpl>(m_ctx, *this);
    if (!palette->init())
    {
        //can't initialise system palette, fallback to default
        makeLightPalette();
        return;
    }

    m_colorMap.clear();
    int ret = palette->update();
    if (ret != VLC_SUCCESS)
    {
        if (palette->isThemeDark())
            makeDarkPalette();
        else
            makeLightPalette();
    }
    else
    {
        m_isDark = palette->isThemeDark();
    }

    m_palettePriv = std::move(palette);
}
