/*
 * Copyright (C) 2013  Daniel Vrátil <dvratil@redhat.com>
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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#include "behavior-config.h"
#include "ui_behavior-config.h"
#include "log-message-view.h"

#include <KDE/KConfig>
#include <KDE/KConfigGroup>
#include <KDE/KDebug>

K_PLUGIN_FACTORY(KCMTelepathyLogViewerBehaviorConfigFactory, registerPlugin<BehaviorConfig>();)
K_EXPORT_PLUGIN(KCMTelepathyLogViewerBehaviorConfigFactory("ktp_logviewer_behavior", "kcm_ktp_logviewer_behavior"))

BehaviorConfig::BehaviorConfig(QWidget *parent, const QVariantList& args)
    : KCModule(KCMTelepathyLogViewerBehaviorConfigFactory::componentData(), parent, args)
    , ui(new Ui::BehaviorConfig())
{

    ui->setupUi(this);

    ui->sortButtonGroup->setId(ui->sortOldestOnTop, LogMessageView::SortOldestTop);
    ui->sortButtonGroup->setId(ui->sortNewestOnTop, LogMessageView::SortNewestTop);

    load();

    connect(ui->sortButtonGroup, SIGNAL(buttonClicked(int)), this, SLOT(onRadioChanged()));
}

BehaviorConfig::~BehaviorConfig()
{
    delete ui;
}

void BehaviorConfig::load()
{
    const KConfig config(QLatin1String("ktelepathyrc"));
    const KConfigGroup group = config.group("LogViewer");
    LogMessageView::SortMode sortMode;
    sortMode = static_cast<LogMessageView::SortMode>(group.readEntry<int>(QLatin1String("SortMode"),
                                                        static_cast<int>(LogMessageView::SortOldestTop)));
    ui->sortOldestOnTop->setChecked(sortMode == LogMessageView::SortOldestTop);
    ui->sortNewestOnTop->setChecked(sortMode == LogMessageView::SortNewestTop);
}

void BehaviorConfig::save()
{
    kDebug();

    KConfig config(QLatin1String("ktelepathyrc"));
    KConfigGroup group = config.group("LogViewer");
    group.writeEntry(QLatin1String("SortMode"), ui->sortButtonGroup->checkedId());
    group.sync();

    Q_EMIT reloadMessages();
}

void BehaviorConfig::changeEvent(QEvent *e)
{
    QWidget::changeEvent(e);
    switch (e->type()) {
    case QEvent::LanguageChange:
        ui->retranslateUi(this);
        break;
    default:
        break;
    }
}

void BehaviorConfig::onRadioChanged()
{
    Q_EMIT changed(true);
}

#include "ui_behavior-config.h"
