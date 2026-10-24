/*****************************************************************************
 * openurl.cpp: Open a MRL or clipboard content
 *****************************************************************************
 * Copyright © 2009 the VideoLAN team
 *
 * Authors: Jean-Philippe André <jpeg@videolan.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "dialogs/open/openurl.hpp"
#include "util/validators.hpp"

#include <QPushButton>
#include <QDialogButtonBox>
#include <QApplication>
#include <QClipboard>
#include <QFile>
#include <QLabel>
#include <QVBoxLayout>
#include <QLineEdit>

#include "maininterface/mainctx.hpp"
#include "medialibrary/medialib.hpp"
#include <vlc_media_library.h>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QFrame>
#include <QWindow>
#include <QMouseEvent>
#include <QGraphicsDropShadowEffect>
#include <QEvent>

#include <cassert>
#include <algorithm>
#include <vector>

OpenUrlDialog::OpenUrlDialog( qt_intf_t *_p_intf,
                              bool _bClipboard ) :
        QVLCDialog( nullptr, _p_intf ), bClipboard( _bClipboard )
{
    setWindowTitle( qtr( "Open URL" ) );
    setWindowRole( "vlc-open-url" );

    /* Buttons */
    QPushButton *but;

    QDialogButtonBox *box = new QDialogButtonBox( this );
    but = box->addButton( qtr( "&Play" ), QDialogButtonBox::AcceptRole );
    connect( but, &QPushButton::clicked, this, &OpenUrlDialog::play );

    but = box->addButton( qtr( "&Enqueue" ), QDialogButtonBox::AcceptRole );
    connect( but, &QPushButton::clicked, this, &OpenUrlDialog::enqueue );

    but = box->addButton( qtr( "&Cancel" ) , QDialogButtonBox::RejectRole );
    connect( box, &QDialogButtonBox::rejected, this, &OpenUrlDialog::reject );

    /* Info label and line edit */
    edit = new QLineEdit( this );
    edit->setPlaceholderText( qtr( "Enter URL here..." ) );
    edit->setValidator( new UrlValidator( edit ) );

    QLabel *info = new QLabel( qtr( "Please enter the URL or path "
                                    "to the media you want to play."),
                               this );

    setToolTip( qtr( "If your clipboard contains a valid URL\n"
                     "or the path to a file on your computer,\n"
                     "it will be automatically selected." ) );

    /* Layout */
    QVBoxLayout *vlay = new QVBoxLayout( this );

    vlay->addWidget( info );
    vlay->addWidget( edit );
    vlay->addWidget( box );
}

void OpenUrlDialog::enqueue()
{
    bShouldEnqueue = true;
    accept();
}

void OpenUrlDialog::play()
{
    accept();
}

QString OpenUrlDialog::url() const
{
    return lastUrl;
}

bool OpenUrlDialog::shouldEnqueue() const
{
    return bShouldEnqueue;
}

/** Show Event:
 * When the dialog is shown, try to extract a URL from the clipboard
 * and paste it in the Edit box.
 * showEvent can happen not only on exec() but I think it's cool to
 * actualize the URL on showEvent (eg. change virtual desktop...)
 **/
void OpenUrlDialog::showEvent( QShowEvent* )
{
    bShouldEnqueue = false;
    edit->setFocus( Qt::OtherFocusReason );
    if( !lastUrl.isEmpty() && edit->text().isEmpty() )
    {
        /* The text should not have been changed, excepted if the user
        has clicked Cancel before */
        edit->setText( lastUrl );
    }
    else
        edit->clear();

    if( bClipboard )
    {
        QClipboard *clipboard = QApplication::clipboard();
        assert( clipboard != NULL );
        QString txt = clipboard->text( QClipboard::Selection ).trimmed();

        if( txt.isEmpty() || ( !txt.contains("://") && !QFile::exists(txt) ) )
            txt = clipboard->text( QClipboard::Clipboard ).trimmed();

        if( txt.contains( "://" ) || QFile::exists( txt ) )
            edit->setText( txt );
    }
}

void OpenUrlDialog::accept()
{
    lastUrl = edit->text().trimmed();
    QVLCDialog::accept();
}

namespace {
class NetworkDialogHeader : public QFrame
{
public:
    using QFrame::QFrame;
protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && window()->windowHandle())
            window()->windowHandle()->startSystemMove();
        QFrame::mousePressEvent(event);
    }
};
}

NetworkStreamDialog::NetworkStreamDialog(qt_intf_t *intf)
    : QVLCDialog(nullptr, intf)
{
    setWindowTitle(qtr("Open Network Stream"));
    setWindowRole("vlc-network-stream");
    setWindowFlags(windowFlags() | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setMinimumSize(560, 460);
    resize(QSize(1076, 659).boundedTo(screen()->availableGeometry().size()));

    auto outer = new QVBoxLayout(this);
    outer->setContentsMargins(14, 6, 14, 22);
    auto panel = new QFrame(this);
    m_panel = panel;
    panel->setObjectName("networkPanel");
    auto shadow = new QGraphicsDropShadowEffect(panel);
    shadow->setBlurRadius(15);
    shadow->setOffset(0, 8);
    shadow->setColor(QColor(0, 0, 0, 117));
    panel->setGraphicsEffect(shadow);
    outer->addWidget(panel);
    auto layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto header = new NetworkDialogHeader(panel);
    header->setObjectName("networkHeader");
    header->setFixedHeight(50);
    auto headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(19, 9, 9, 9);
    headerLayout->addWidget(new QLabel(windowTitle(), header));
    headerLayout->addStretch();
    auto close = new QPushButton(header);
    close->setObjectName("networkClose");
    close->setAccessibleName(qtr("Close"));
    close->setIcon(QIcon(QStringLiteral(":/qt/modern/close.svg")));
    close->setIconSize(QSize(24, 24));
    close->setFixedSize(32, 32);
    connect(close, &QPushButton::clicked, this, &NetworkStreamDialog::reject);
    headerLayout->addWidget(close);
    layout->addWidget(header);

    auto body = new QVBoxLayout;
    body->setContentsMargins(20, 22, 20, 20);
    body->setSpacing(20);
    auto description = [panel](const QString& title, const QString& detail) {
        auto column = new QVBoxLayout;
        column->setSpacing(5);
        auto heading = new QLabel(title, panel);
        heading->setObjectName("networkHeading");
        heading->setMinimumHeight(28);
        auto subtitle = new QLabel(detail, panel);
        subtitle->setObjectName("networkDetail");
        subtitle->setWordWrap(true);
        subtitle->setMinimumHeight(25);
        column->addWidget(heading);
        column->addWidget(subtitle);
        return column;
    };
    body->addLayout(description(qtr("Network URL"), qtr("You can enter multiple links to load.")));
    m_urls = new QPlainTextEdit(panel);
    m_urls->setObjectName("networkUrls");
    m_urls->setAccessibleName(qtr("Network URLs, one per line"));
    m_urls->setPlaceholderText(qtr("e.g., https://www.example.com/stream.avi"));
    m_urls->setTabChangesFocus(true);
    m_urls->setMinimumHeight(100);
    body->addWidget(m_urls, 1);
    auto divider = new QLabel(panel);
    divider->setPixmap(QPixmap(QStringLiteral(":/qt/modern/divider.svg")));
    divider->setScaledContents(true);
    divider->setFixedHeight(1);
    body->addWidget(divider);
    body->addLayout(description(qtr("Playlist Name (optional)"),
                               qtr("The playlist will be saved and displayed with this title.")));
    m_name = new QLineEdit(panel);
    m_name->setObjectName("networkPlaylistName");
    m_name->setAccessibleName(qtr("Playlist name"));
    m_name->setPlaceholderText(qtr("e.g., Favorite TV Channels"));
    m_name->setFixedHeight(41);
    body->addWidget(m_name);
    m_error = new QLabel(panel);
    m_error->setWordWrap(true);
    m_error->setObjectName("networkError");
    m_error->hide();
    body->addWidget(m_error);
    body->addSpacing(35);
    layout->addLayout(body, 1);

    auto buttons = new QHBoxLayout;
    buttons->setContentsMargins(20, 20, 20, 20);
    auto cancel = new QPushButton(qtr("Cancel"), panel);
    cancel->setFixedSize(100, 32);
    connect(cancel, &QPushButton::clicked, this, &NetworkStreamDialog::reject);
    buttons->addWidget(cancel);
    buttons->addStretch();
    m_save = new QPushButton(qtr("Save"), panel);
    m_save->setObjectName("networkSave");
    m_save->setFixedSize(100, 32);
    m_save->setDefault(true);
    m_save->setEnabled(false);
    buttons->addWidget(m_save);
    layout->addLayout(buttons);
    connect(m_save, &QPushButton::clicked, this, &NetworkStreamDialog::save);
    connect(m_urls, &QPlainTextEdit::textChanged, this, &NetworkStreamDialog::validate);
    applyTheme();
    m_urls->setFocus();
}

void NetworkStreamDialog::changeEvent(QEvent *event)
{
    QVLCDialog::changeEvent(event);
    if (event->type() == QEvent::PaletteChange && m_panel)
        applyTheme();
}

void NetworkStreamDialog::applyTheme()
{
    const auto colors = palette();
    const bool dark = colors.color(QPalette::Window).lightness() < 128;
    const auto accent = colors.color(QPalette::Highlight);
    const auto surface = colors.color(QPalette::Window);
    // A translucent accent over the field color follows custom accents too.
    const auto field = colors.color(QPalette::Base);
    const auto tint = QColor::fromRgbF(accent.redF() * .24 + field.redF() * .76,
                                      accent.greenF() * .24 + field.greenF() * .76,
                                      accent.blueF() * .24 + field.blueF() * .76);
    const QString css = QStringLiteral(
        "QFrame#networkPanel { background: %1; border-radius: 14px; }"
        "QWidget { color: %2; font: 14px 'Inter'; font-weight: normal; }"
        "QFrame#networkHeader { background: %3; border-top-left-radius: 14px; border-top-right-radius: 14px; }"
        "QLabel { background: transparent; }"
        "QLabel#networkHeading { color: %8; font-size: 16px; }"
        "QLabel#networkDetail { color: %9; }"
        "QLabel#networkError { color: %10; }"
        "QPlainTextEdit, QLineEdit { background: %3; border: 2px solid %1; border-radius: 6px; padding: 8px 12px; selection-background-color: %6; }"
        "QPlainTextEdit:focus, QLineEdit:focus { border-color: %5; }"
        "QPushButton { background: %4; border: 0; border-radius: 6px; font-weight: 600; }"
        "QPushButton:hover { background: %11; }"
        "QPushButton:focus { border: 1px solid %5; }"
        "QPushButton#networkSave { color: %5; background: %6; }"
        "QPushButton#networkSave:disabled { color: %7; background: %1; }"
        "QPushButton#networkClose { background: transparent; }")
        .arg(colors.color(QPalette::AlternateBase).name(),
             colors.color(QPalette::WindowText).name(), field.name(), surface.name(),
             accent.name(), tint.name(), colors.color(QPalette::Disabled, QPalette::Text).name(QColor::HexArgb),
             dark ? QStringLiteral("#c6c6c6") : QStringLiteral("#2e2e2e"),
             colors.color(QPalette::PlaceholderText).name(),
             dark ? QStringLiteral("#e15032") : QStringLiteral("#a52d1b"),
             (dark ? surface.lighter(125) : surface.darker(108)).name());
    m_panel->setStyleSheet(css);
    if (auto close = m_panel->findChild<QPushButton*>(QStringLiteral("networkClose")))
        close->setIcon(QIcon(dark ? QStringLiteral(":/qt/modern/close.svg")
                                 : QStringLiteral(":/qt/modern/close-light.svg")));
}

QStringList NetworkStreamDialog::urls() const
{
    QStringList result;
    const auto lines = m_urls->toPlainText().split(QRegularExpression(QStringLiteral("[\r\n]+")), Qt::SkipEmptyParts);
    for (const auto& line : lines)
    {
        const auto text = line.trimmed();
        if (!text.isEmpty())
            result.push_back(QUrl(text, QUrl::TolerantMode).toString(QUrl::FullyEncoded));
    }
    return result;
}

void NetworkStreamDialog::validate()
{
    const auto entries = urls();
    const bool valid = !entries.isEmpty() && std::all_of(entries.cbegin(), entries.cend(), [](const QString& text) {
        const QUrl url(text, QUrl::StrictMode);
        return url.isValid() && !url.scheme().isEmpty();
    });
    m_save->setEnabled(valid && !m_saving);
    m_error->hide();
}

void NetworkStreamDialog::reject()
{
    // A submitted library write finishes before the modal can be dismissed.
    if (!m_saving)
        QVLCDialog::reject();
}

void NetworkStreamDialog::save()
{
    if (!m_save->isEnabled() || m_saving)
        return;
    auto library = p_intf->p_mi->getMediaLibrary();
    if (!library)
    {
        if (!m_name->text().trimmed().isEmpty())
        {
            m_error->setText(qtr("The media library is unavailable. Clear the playlist name to open the streams without saving a playlist."));
            m_error->show();
            return;
        }
        accept();
        return;
    }
    const auto entries = urls();
    auto name = m_name->text().trimmed();
    if (name.isEmpty())
        name = qtr("Network streams");
    m_saving = true;
    m_save->setEnabled(false);
    m_urls->setReadOnly(true);
    m_name->setReadOnly(true);
    struct Result { bool success = false; };
    library->runOnMLThread<Result>(this,
        [name, entries](vlc_medialibrary_t* ml, Result& result) {
            auto playlist = vlc_ml_playlist_create(ml, qtu(name));
            if (!playlist)
                return;
            const auto id = playlist->i_id;
            vlc_ml_playlist_release(playlist);
            std::vector<int64_t> ids;
            ids.reserve(entries.size());
            for (const auto& url : entries)
            {
                auto media = vlc_ml_get_media_by_mrl(ml, qtu(url));
                if (!media)
                    media = vlc_ml_new_external_media(ml, qtu(url));
                if (!media)
                    break;
                ids.push_back(media->i_id);
                vlc_ml_media_release(media);
            }
            result.success = ids.size() == static_cast<size_t>(entries.size())
                && vlc_ml_playlist_append(ml, id, ids.data(), ids.size()) == VLC_SUCCESS;
            if (!result.success)
                vlc_ml_playlist_delete(ml, id);
        },
        [this](quint64, Result& result) {
            m_saving = false;
            if (result.success)
                accept();
            else
            {
                m_urls->setReadOnly(false);
                m_name->setReadOnly(false);
                validate();
                m_error->setText(qtr("Could not save the playlist. Please try again."));
                m_error->show();
            }
        });
}
