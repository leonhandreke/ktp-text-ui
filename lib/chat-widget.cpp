/***************************************************************************
 *   Copyright (C) 2010 by David Edmundson <kde@davidedmundson.co.uk>      *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA            *
 ***************************************************************************/

#include "chat-widget.h"

#include "ui_chat-widget.h"
#include "adium-theme-header-info.h"
#include "adium-theme-content-info.h"
#include "adium-theme-message-info.h"
#include "adium-theme-status-info.h"
#include "channel-contact-model.h"
#include "notify-filter.h"
#include "text-chat-config.h"

#include <QtGui/QKeyEvent>
#include <QtGui/QAction>
#include <QSortFilterProxyModel>
#include <QDeclarativeEngine>
#include <QDeclarativeContext>

#include <KColorDialog>
#include <KNotification>
#include <KAboutData>
#include <KComponentData>
#include <KDebug>
#include <KColorScheme>
#include <KLineEdit>
#include <KMimeType>
#include <KTemporaryFile>

#include <TelepathyQt/Account>
#include <TelepathyQt/Message>
#include <TelepathyQt/Types>
#include <TelepathyQt/AvatarData>
#include <TelepathyQt/Connection>
#include <TelepathyQt/Presence>
#include <TelepathyQt/PendingChannelRequest>
#include <TelepathyQt/OutgoingFileTransferChannel>

#include <KTp/presence.h>
#include <KTp/actions.h>
#include <KTp/message-processor.h>
#include <KTp/conversation.h>
#include <KTp/Logger/scrollback-manager.h>

#include <sonnet/speller.h>

class ChatWidgetPrivate
{
public:
    ChatWidgetPrivate() :
        remoteContactChatState(Tp::ChannelChatStateInactive),
        isGroupChat(false)
    {
    }
    /** Stores whether the channel is ready with all contacts upgraded*/
    Tp::ChannelChatState remoteContactChatState;
    QString contactName;
    QString yourName;
    Tp::TextChannelPtr channel;
    Tp::AccountPtr account;
    Ui::ChatWidget ui;
    ChannelContactModel *contactModel;
    ScrollbackManager *logManager;
    QTimer *pausedStateTimer;
    bool isGroupChat;
    QList< Tp::OutgoingFileTransferChannelPtr > tmpFileTransfers;

    KComponentData telepathyComponentData();
    NotifyFilter *notifyFilter;
    Conversation *conversation;
};


//FIXME I would like this to be part of the main KDE Telepathy library as a static function somewhere.
KComponentData ChatWidgetPrivate::telepathyComponentData()
{
    KAboutData telepathySharedAboutData("ktelepathy",0,KLocalizedString(),0);
    return KComponentData(telepathySharedAboutData);
}

ChatWidget::ChatWidget(const Tp::TextChannelPtr & channel, const Tp::AccountPtr &account, QWidget *parent)
    : QWidget(parent),
      d(new ChatWidgetPrivate)
{
    //load translations for this library. keep this before any i18n() calls in library code
    KGlobal::locale()->insertCatalog(QLatin1String("ktpchat"));

    d->channel = channel;
    d->account = account;
    d->ui.setupUi(this);

    d->conversation = new Conversation(channel, account, this);

    d->notifyFilter = new NotifyFilter(this);
    connect(d->conversation->messages(), SIGNAL(messageReceived(KTp::Message)), d->notifyFilter, SLOT(sendMessageNotification(KTp::Message)));

    d->ui.declarativeView->engine()->rootContext()->setContextProperty(QLatin1String("conversation"), d->conversation);

    d->ui.declarativeView->engine()->addImportPath(QLatin1String("/opt/kde4/lib/kde4/imports")); //FIXME. KDeclarative??
    d->ui.declarativeView->setSource(QUrl(QLatin1String("/home/david/projects/temp/ktp_text_qml/take2/take2.qml")));

    d->logManager = new ScrollbackManager(this);
    connect(d->logManager, SIGNAL(fetched(QList<KTp::Message>)), SLOT(onHistoryFetched(QList<KTp::Message>)));

    connect(d->account.data(), SIGNAL(currentPresenceChanged(Tp::Presence)),
            this, SLOT(currentPresenceChanged(Tp::Presence)));

    if (channel->targetHandleType() == Tp::HandleTypeContact) {
        d->isGroupChat = false;
        d->ui.contactsView->hide();
    } else {
        d->isGroupChat = true;
        d->ui.sendMessageBox->setContactModel(d->contactModel);
    }

//     d->ui.chatArea->setTextChannel(account, channel);

    // connect channel signals
    setupChannelSignals();

    // create contactModel and start keeping track of contacts.
    d->contactModel = new ChannelContactModel(d->channel, this);
    setupContactModelSignals();

    QSortFilterProxyModel *sortModel = new QSortFilterProxyModel(this);
    sortModel->setSourceModel(d->contactModel);
    sortModel->setSortRole(Qt::DisplayRole);
    sortModel->setSortCaseSensitivity(Qt::CaseInsensitive);
    sortModel->setSortLocaleAware(true);
    sortModel->setDynamicSortFilter(true);
    sortModel->sort(0);

    d->ui.contactsView->setModel(sortModel);

    d->yourName = channel->groupSelfContact()->alias();

    d->ui.sendMessageBox->setAcceptDrops(false);
//     d->ui.chatArea->setAcceptDrops(false);
    setAcceptDrops(true);

    /* Prepare the chat area */
//     connect(d->ui.chatArea, SIGNAL(zoomFactorChanged(qreal)), SIGNAL(zoomFactorChanged(qreal)));
//     connect(d->ui.chatArea, SIGNAL(textPasted()), d->ui.sendMessageBox, SLOT(pasteSelection()));

    d->pausedStateTimer = new QTimer(this);
    d->pausedStateTimer->setSingleShot(true);

    // Spellchecking set up will trigger textChanged() signal of d->ui.sendMessageBox
    // and our handler checks state of the timer created above.
    loadSpellCheckingOption();

    // make clicking in the main HTML area put focus in the input text box
    //d->ui.chatArea->setFocusProxy(d->ui.sendMessageBox);
    //make activating the tab select the text area
    //setFocusProxy(d->ui.sendMessageBox);

    connect(d->ui.sendMessageBox, SIGNAL(returnKeyPressed()), SLOT(sendMessage()));

    connect(d->ui.searchBar, SIGNAL(findTextSignal(QString,QWebPage::FindFlags)), this, SLOT(findTextInChat(QString,QWebPage::FindFlags)));
    connect(d->ui.searchBar, SIGNAL(findNextSignal(QString,QWebPage::FindFlags)), this, SLOT(findNextTextInChat(QString,QWebPage::FindFlags)));
    connect(d->ui.searchBar, SIGNAL(findPreviousSignal(QString,QWebPage::FindFlags)), this, SLOT(findPreviousTextInChat(QString,QWebPage::FindFlags)));
    connect(d->ui.searchBar, SIGNAL(flagsChangedSignal(QString,QWebPage::FindFlags)), this, SLOT(findTextInChat(QString,QWebPage::FindFlags)));

    connect(this, SIGNAL(searchTextComplete(bool)), d->ui.searchBar, SLOT(onSearchTextComplete(bool)));

    connect(d->pausedStateTimer, SIGNAL(timeout()), this, SLOT(onChatPausedTimerExpired()));
}

ChatWidget::~ChatWidget()
{
    saveSpellCheckingOption();
    d->channel->requestClose(); //tell Tp we are no longer want to handle this channel; does nothing if already closed
    delete d;
}

void ChatWidget::changeEvent(QEvent *e)
{
    QWidget::changeEvent(e);
    switch (e->type()) {
    case QEvent::LanguageChange:
        d->ui.retranslateUi(this);
        break;
    default:
        break;
    }
}

void ChatWidget::showEvent(QShowEvent *)
{
    d->conversation->setVisibleToUser(true);
}

void ChatWidget::hideEvent(QHideEvent *)
{
    d->conversation->setVisibleToUser(false);
}

void ChatWidget::resizeEvent(QResizeEvent *e)
{
    //set the maximum height of a text box to a third of the total window height (but no smaller than the minimum size)
    int textBoxHeight = e->size().height() / 3;
    if (textBoxHeight < d->ui.sendMessageBox->minimumSizeHint().height()) {
        textBoxHeight = d->ui.sendMessageBox->minimumSizeHint().height();
    }
    d->ui.sendMessageBox->setMaximumHeight(textBoxHeight);
    QWidget::resizeEvent(e);
}

Tp::AccountPtr ChatWidget::account() const
{
    return d->account;
}

KIcon ChatWidget::icon() const
{
    if (d->account->currentPresence() != Tp::Presence::offline()) {
        //normal chat - self and one other person.
        if (!d->isGroupChat) {
            //find the other contact which isn't self.
            Tp::ContactPtr otherContact = d->channel->targetContact();
            return KTp::Presence(otherContact->presence()).icon();
        }
        else {
            //group chat
            return KTp::Presence(Tp::Presence::available()).icon();
        }
    }
    return KTp::Presence(Tp::Presence::offline()).icon();
}

KIcon ChatWidget::accountIcon() const
{
    return KIcon(d->account->iconName());
}

bool ChatWidget::isGroupChat() const
{
    return d->isGroupChat;
}

ChatSearchBar *ChatWidget::chatSearchBar() const
{
    return d->ui.searchBar;
}

void ChatWidget::setChatEnabled(bool enable)
{
    d->ui.contactsView->setEnabled(enable);
    d->ui.sendMessageBox->setEnabled(enable);
    Q_EMIT iconChanged(icon());
}

void ChatWidget::setTextChannel(const Tp::TextChannelPtr &newTextChannelPtr)
{

    d->channel = newTextChannelPtr;     // set the new channel
    d->contactModel->setTextChannel(newTextChannelPtr);
//     d->ui.chatArea->setTextChannel(d->account, newTextChannelPtr);

    // connect signals for the new textchannel
    setupChannelSignals();

    setChatEnabled(true);
    onContactPresenceChange(d->channel->groupSelfContact(), KTp::Presence(d->channel->groupSelfContact()->presence()));
}

Tp::TextChannelPtr ChatWidget::textChannel() const
{
    return d->channel;
}

void ChatWidget::keyPressEvent(QKeyEvent *e)
{
    if (e->matches(QKeySequence::Copy)) {
//         d->ui.chatArea->triggerPageAction(QWebPage::Copy);
        return;
    }

    if (e->key() == Qt::Key_PageUp ||
        e->key() == Qt::Key_PageDown) {
//         d->ui.chatArea->event(e);
        return;
    }

    QWidget::keyPressEvent(e);
}

void ChatWidget::temporaryFileTransferStateChanged(Tp::FileTransferState state, Tp::FileTransferStateChangeReason reason)
{
    Q_UNUSED(reason);

    if ((state == Tp::FileTransferStateCompleted) || (state == Tp::FileTransferStateCancelled)) {
        Tp::OutgoingFileTransferChannel *channel = qobject_cast<Tp::OutgoingFileTransferChannel*>(sender());
        Q_ASSERT(channel);

        QString localFile = QUrl(channel->uri()).toLocalFile();
        if (QFile::exists(localFile)) {
            QFile::remove(localFile);
            kDebug() << "File" << localFile << "removed";
        }

        d->tmpFileTransfers.removeAll(Tp::OutgoingFileTransferChannelPtr(channel));
    }
}


void ChatWidget::temporaryFileTransferChannelCreated(Tp::PendingOperation *operation)
{
    Tp::PendingChannelRequest *request = qobject_cast<Tp::PendingChannelRequest*>(operation);
    Q_ASSERT(request);

    Tp::OutgoingFileTransferChannelPtr transferChannel;
    transferChannel = Tp::OutgoingFileTransferChannelPtr::qObjectCast<Tp::Channel>(request->channelRequest()->channel());
    Q_ASSERT(!transferChannel.isNull());

    /* Make sure the pointer lives until the transfer is over
     * so that the signal connection below lasts until the end */
    d->tmpFileTransfers << transferChannel;

    connect(transferChannel.data(), SIGNAL(stateChanged(Tp::FileTransferState,Tp::FileTransferStateChangeReason)),
            this, SLOT(temporaryFileTransferStateChanged(Tp::FileTransferState,Tp::FileTransferStateChangeReason)));
}


void ChatWidget::dropEvent(QDropEvent *e)
{
    const QMimeData *data = e->mimeData();

    if (data->hasUrls()) {
        Q_FOREACH(const QUrl &url, data->urls()) {
            if (url.isLocalFile()) {
        KTp::Actions::startFileTransfer(d->account, d->channel->targetContact(), url.toLocalFile());
            } else {
                d->ui.sendMessageBox->append(url.toString());
            }
        }
        e->acceptProposedAction();
    } else if (data->hasText()) {
        d->ui.sendMessageBox->append(data->text());
        e->acceptProposedAction();
    } else if (data->hasHtml()) {
        d->ui.sendMessageBox->insertHtml(data->html());
        e->acceptProposedAction();
    } else if (data->hasImage()) {
        QImage image = qvariant_cast<QImage>(data->imageData());

        KTemporaryFile tmpFile;
        tmpFile.setPrefix(d->account->displayName() + QLatin1String("-"));
        tmpFile.setSuffix(QLatin1String(".png"));
        tmpFile.setAutoRemove(false);
        if (!tmpFile.open()) {
            return;
        }
        tmpFile.close();

        if (!image.save(tmpFile.fileName(), "PNG")) {
            return;
        }

        Tp::PendingChannelRequest *request;
    request = KTp::Actions::startFileTransfer(d->account, d->channel->targetContact(), tmpFile.fileName());
        connect(request, SIGNAL(finished(Tp::PendingOperation*)),
                this, SLOT(temporaryFileTransferChannelCreated(Tp::PendingOperation*)));

        kDebug() << "Starting transfer of" << tmpFile.fileName();
        e->acceptProposedAction();
    }

    QWidget::dropEvent(e);
}

void ChatWidget::dragEnterEvent(QDragEnterEvent *e)
{
    if (e->mimeData()->hasHtml() || e->mimeData()->hasImage() ||
        e->mimeData()->hasText() || e->mimeData()->hasUrls()) {
            e->accept();
    }

    QWidget::dragEnterEvent(e);
}

QString ChatWidget::title() const
{
    return d->conversation->title();
}

QColor ChatWidget::titleColor() const
{
    /*return a color to set the tab text as in order of importance
    typing
    unread messages
    user offline

    */

    KColorScheme scheme(QPalette::Active, KColorScheme::Window);

    if (TextChatConfig::instance()->showOthersTyping() && (d->remoteContactChatState == Tp::ChannelChatStateComposing)) {
        kDebug() << "remote is typing";
        return scheme.foreground(KColorScheme::PositiveText).color();
    }

    if (unreadMessageCount() > 0 && !isOnTop()) {
        kDebug() << "unread messages";
        return scheme.foreground(KColorScheme::ActiveText).color();
    }

    //normal chat - self and one other person.
    if (!d->isGroupChat) {
        //find the other contact which isn't self.
        Q_FOREACH(const Tp::ContactPtr & contact, d->channel->groupContacts()) {
            if (contact != d->channel->groupSelfContact()) {
                if (contact->presence().type() == Tp::ConnectionPresenceTypeOffline ||
                    contact->presence().type() == Tp::ConnectionPresenceTypeHidden) {
                    return scheme.foreground(KColorScheme::InactiveText).color();
                }
            }
        }
    }

    return scheme.foreground(KColorScheme::NormalText).color();
}

void ChatWidget::toggleSearchBar() const
{
    if(d->ui.searchBar->isVisible()) {
        d->ui.searchBar->toggleView(false);
    } else {
        d->ui.searchBar->toggleView(true);
    }
}

void ChatWidget::setupChannelSignals()
{
    connect(d->channel.data(), SIGNAL(messageReceived(Tp::ReceivedMessage)),
            SIGNAL(unreadMessagesChanged()));
    connect(d->channel.data(), SIGNAL(pendingMessageRemoved(Tp::ReceivedMessage)),
            SIGNAL(unreadMessagesChanged()));
    connect(d->channel.data(), SIGNAL(chatStateChanged(Tp::ContactPtr,Tp::ChannelChatState)),
            SLOT(onChatStatusChanged(Tp::ContactPtr,Tp::ChannelChatState)));
    connect(d->channel.data(), SIGNAL(invalidated(Tp::DBusProxy*,QString,QString)),
            this, SLOT(onChannelInvalidated()));

    if (d->channel->hasChatStateInterface()) {
        connect(d->ui.sendMessageBox, SIGNAL(textChanged()), SLOT(onInputBoxChanged()));
    }
}

void ChatWidget::setupContactModelSignals()
{
    connect(d->contactModel, SIGNAL(contactPresenceChanged(Tp::ContactPtr,KTp::Presence)),
            SLOT(onContactPresenceChange(Tp::ContactPtr,KTp::Presence)));
    connect(d->contactModel, SIGNAL(contactAliasChanged(Tp::ContactPtr,QString)),
            SLOT(onContactAliasChanged(Tp::ContactPtr,QString)));
    connect(d->contactModel, SIGNAL(contactBlockStatusChanged(Tp::ContactPtr,bool)),
       SLOT(onContactBlockStatusChanged(Tp::ContactPtr,bool)));
}

int ChatWidget::unreadMessageCount() const
{
    return d->channel->messageQueue().size();
}

void ChatWidget::acknowledgeMessages()
{
    d->conversation->messages()->acknowledgeAllMessages();
}

void ChatWidget::updateSendMessageShortcuts(const KShortcut &shortcuts)
{
    d->ui.sendMessageBox->setSendMessageShortcuts(shortcuts);
}

bool ChatWidget::isOnTop() const
{
    kDebug() << ( isActiveWindow() && isVisible() );
    return ( isActiveWindow() && isVisible() );
}

void ChatWidget::sendMessage()
{
    QString message = d->ui.sendMessageBox->toPlainText();

    if (!message.isEmpty()) {
        message = KTp::MessageProcessor::instance()->processOutgoingMessage(
                    message, d->account, d->channel).text();

        if (d->channel->supportsMessageType(Tp::ChannelTextMessageTypeAction) && message.startsWith(QLatin1String("/me "))) {
            //remove "/me " from the start of the message
            message.remove(0,4);

            d->channel->send(message, Tp::ChannelTextMessageTypeAction);
        } else {
            d->channel->send(message);
        }
        d->ui.sendMessageBox->clear();
    }
}

void ChatWidget::onChatStatusChanged(const Tp::ContactPtr & contact, Tp::ChannelChatState state)
{
    //don't show our own status changes.
    if (contact == d->channel->groupSelfContact()) {
        return;
    }

    if (state == Tp::ChannelChatStateGone) {
//         d->ui.chatArea->addStatusMessage(i18n("%1 has left the chat", contact->alias()));
    }

    if (d->isGroupChat) {
        //In a multiperson chat just because this user is no longer typing it doesn't mean that no-one is.
        //loop through each contact, check no-one is in composing mode.

        Tp::ChannelChatState tempState = Tp::ChannelChatStateInactive;

        Q_FOREACH (const Tp::ContactPtr & contact, d->channel->groupContacts()) {
            if (contact == d->channel->groupSelfContact()) {
                continue;
            }

            tempState = d->channel->chatState(contact);

            if (tempState == Tp::ChannelChatStateComposing) {
                state = tempState;
                break;
            } else if (tempState == Tp::ChannelChatStatePaused && state != Tp::ChannelChatStateComposing) {
                state = tempState;
            }
        }
    }

    if (state != d->remoteContactChatState) {
        d->remoteContactChatState = state;
        Q_EMIT userTypingChanged(state);
    }
}



void ChatWidget::onContactPresenceChange(const Tp::ContactPtr & contact, const KTp::Presence &presence)
{
    QString message;
    bool isYou = (contact == d->channel->groupSelfContact());

    if (isYou) {
        if (presence.statusMessage().isEmpty()) {
            message = i18nc("Your presence status", "You are now marked as %1",
                            presence.displayString());
        } else {
            message = i18nc("Your presence status with status message",
                            "You are now marked as %1 - %2",
                            presence.displayString(), presence.statusMessage());
        }
    }
    else {
        if (presence.statusMessage().isEmpty()) {
            message = i18nc("User's name, with their new presence status (i.e online/away)","%1 is %2", contact->alias(), presence.displayString());
        } else {
            message = i18nc("User's name, with their new presence status (i.e online/away) and a sepecified presence message","%1 is %2 - %3",
                            contact->alias(),
                            presence.displayString(),
                            presence.statusMessage());
        }
    }

    if (!message.isNull()) {
//         if (d->ui.chatArea->showPresenceChanges()) {
//             d->ui.chatArea->addStatusMessage(message);
//         }
    }

    //if in a non-group chat situation, and the other contact has changed state...
//     if (!d->isGroupChat && !isYou) {
//         Q_EMIT iconChanged(presence.icon());
//     }

//     Q_EMIT contactPresenceChanged(presence);
}

void ChatWidget::onContactAliasChanged(const Tp::ContactPtr & contact, const QString& alias)
{
    QString message;
    bool isYou = (contact == d->channel->groupSelfContact());

    if (isYou) {
        if (d->yourName != alias) {
            message = i18n("You are now known as %1", alias);
            d->yourName = alias;
        }
    } else if (!d->isGroupChat) {
        //HACK the title is the contact alias on non-groupchats,
        //but we should have a better way of keeping the previous
        //aliases of all contacts
        if (d->contactName != alias) {
            message = i18n("%1 is now known as %2", d->contactName, alias);
            d->contactName = alias;
        }
    }

    if (!message.isEmpty()) {
//         d->ui.chatArea->addStatusMessage(i18n("%1 has left the chat", contact->alias()));
    }

    //if in a non-group chat situation, and the other contact has changed alias...
    if (!d->isGroupChat && !isYou) {
        Q_EMIT titleChanged(alias);
    }
}

void ChatWidget::onContactBlockStatusChanged(const Tp::ContactPtr &contact, bool blocked)
{
    QString message;
    if(blocked) {
        message = i18n("%1 is now blocked.", contact->alias());
    } else {
        message = i18n("%1 is now unblocked.", contact->alias());
    }

//     d->ui.chatArea->addStatusMessage(message);

    Q_EMIT contactBlockStatusChanged(blocked);
}

void ChatWidget::onChannelInvalidated()
{
    setChatEnabled(false);
}

void ChatWidget::onInputBoxChanged()
{
    //if the box is empty
    bool textBoxEmpty = d->ui.sendMessageBox->toPlainText().isEmpty();

    if(!textBoxEmpty) {
        //if the timer is active, it means the user is continuously typing
        if (d->pausedStateTimer->isActive()) {
            //just restart the timer and don't spam with chat state changes
            d->pausedStateTimer->start(5000);
        } else {
            //if the user has just typed some text, set state to Composing and start the timer
            //unless "show me typing" is off; in that case set state to Active and stop the timer
            if (TextChatConfig::instance()->showMeTyping()) {
                d->channel->requestChatState(Tp::ChannelChatStateComposing);
                d->pausedStateTimer->start(5000);
            } else {
                d->channel->requestChatState(Tp::ChannelChatStateActive);
                d->pausedStateTimer->stop();
            }
        }
    } else {
        //if the user typed no text/cleared the input field, set Active and stop the timer
        d->channel->requestChatState(Tp::ChannelChatStateActive);
        d->pausedStateTimer->stop();
    }
}

void ChatWidget::findTextInChat(const QString& text, QWebPage::FindFlags flags)
{
    // reset highlights
//     d->ui.chatArea->findText(QString(), flags);

//     if(d->ui.chatArea->findText(text, flags)) {
//         Q_EMIT searchTextComplete(true);
//     } else {
//         Q_EMIT searchTextComplete(false);
//     }
}

void ChatWidget::findNextTextInChat(const QString& text, QWebPage::FindFlags flags)
{
//     d->ui.chatArea->findText(text, flags);
}

void ChatWidget::findPreviousTextInChat(const QString& text, QWebPage::FindFlags flags)
{
    // for "backwards" search
//     flags |= QWebPage::FindBackward;
//     d->ui.chatArea->findText(text, flags);
}

void ChatWidget::setSpellDictionary(const QString &dict)
{
    d->ui.sendMessageBox->setSpellCheckingLanguage(dict);
}

void ChatWidget::saveSpellCheckingOption()
{
    QString spellCheckingLanguage = spellDictionary();
    KSharedConfigPtr config = KSharedConfig::openConfig(QLatin1String("ktp-text-uirc"));
    KConfigGroup configGroup = config->group(d->channel->targetId());
    if (spellCheckingLanguage != Sonnet::Speller().defaultLanguage()) {
        configGroup.writeEntry("language", spellCheckingLanguage);
    } else {
        if (configGroup.exists()) {
            configGroup.deleteEntry("language");
            configGroup.deleteGroup();
        } else {
            return;
        }
    }
    configGroup.sync();
}

void ChatWidget::loadSpellCheckingOption()
{
    // KTextEdit::setSpellCheckingLanguage() (see call below) does not do anything if there
    // is no highlighter created yet, and KTextEdit::setCheckSpellingEnabled() does not create
    // it if there is no focus on widget.
    // Therefore d->ui.sendMessageBox->setSpellCheckingLanguage() call below would be is ignored.
    // While this looks like KTextEditor bug (espesially taking into account its documentation),
    // just a call to KTextEditor::createHighlighter() before setting language fixes this
    d->ui.sendMessageBox->createHighlighter();

    KSharedConfigPtr config = KSharedConfig::openConfig(QLatin1String("ktp-text-uirc"));
    KConfigGroup configGroup = config->group(d->channel->targetId());
    QString spellCheckingLanguage;
    if (configGroup.exists()) {
        spellCheckingLanguage = configGroup.readEntry("language");
    } else {
        spellCheckingLanguage = Sonnet::Speller().defaultLanguage();
    }
    d->ui.sendMessageBox->setSpellCheckingLanguage(spellCheckingLanguage);
}

QString ChatWidget::spellDictionary() const
{
    return d->ui.sendMessageBox->spellCheckingLanguage();
}

Tp::ChannelChatState ChatWidget::remoteChatState()
{
    return d->remoteContactChatState;
}

bool ChatWidget::previousConversationAvailable()
{
    return m_previousConversationAvailable;
}

void ChatWidget::clear()
{
    // Don't reload logs when re-initializing */
    //d->logsLoaded = true;
    //d->exchangedMessagesCount = 0;
    //d->ui.sendMessageBox->clearHistory();
    //initChatArea();
}

void ChatWidget::setZoomFactor(qreal zoomFactor)
{
//     d->ui.chatArea->setZoomFactor(zoomFactor);
}

qreal ChatWidget::zoomFactor() const
{
//     return d->ui.chatArea->zoomFactor();
    return 1.0; //FIXME
}

void ChatWidget::onChatPausedTimerExpired()
{
     if (TextChatConfig::instance()->showMeTyping()) {
        d->channel->requestChatState(Tp::ChannelChatStatePaused);
    } else {
        d->channel->requestChatState(Tp::ChannelChatStateActive);
    }
}

void ChatWidget::currentPresenceChanged(const Tp::Presence &presence)
{
    if (presence == Tp::Presence::offline()) {
//         d->ui.chatArea->addStatusMessage(i18n("You are now offline"));
//         Q_EMIT iconChanged(KTp::Presence(Tp::Presence::offline()).icon());
    }
}

void ChatWidget::addEmoticonToChat(const QString &emoticon)
{
    d->ui.sendMessageBox->insertPlainText(QLatin1String(" ") + emoticon);
    d->ui.sendMessageBox->setFocus();
}

void ChatWidget::reloadTheme()
{
//     d->ui.chatArea->reloadTheme();
}

#include "chat-widget.moc"
