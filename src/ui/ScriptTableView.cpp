#include "ui/ScriptTableView.h"

#include <QAbstractItemView>
#include <QHeaderView>
#include <QTableWidget>
#include <QTableWidgetItem>

#include "domain/DeviceRecords.h"

namespace signalbridge
{
void ConfigureScriptTable(QTableWidget* table)
{
    table->setObjectName("SignalBridgePluginScriptTable");
    table->setColumnCount(ScriptTableColumnCount);
    table->setHorizontalHeaderLabels({
        QObject::tr("File Name"),
        QObject::tr("VID"),
        QObject::tr("Device Type"),
        QObject::tr("Script Name"),
        QObject::tr("Publisher"),
    });
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionMode(QAbstractItemView::NoSelection);
    table->setAlternatingRowColors(true);
    table->verticalHeader()->setVisible(false);
    table->horizontalHeader()->setStretchLastSection(true);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
}

void SetScriptTableRows(QTableWidget* table, const QStringList& scripts, bool running)
{
    const int script_count = scripts.size() / ScriptTableColumnCount;
    table->setRowCount(0);
    if(running && scripts.isEmpty())
    {
        table->setRowCount(1);
        table->setItem(0, 0, new QTableWidgetItem(QObject::tr("Scanning SignalRGB scripts...")));
        return;
    }

    if(scripts.isEmpty())
    {
        table->setRowCount(1);
        table->setItem(0, 0, new QTableWidgetItem(QObject::tr("No SignalRGB scripts found.")));
        return;
    }

    table->setRowCount(script_count);
    for(int row = 0; row < script_count; row++)
    {
        for(int column = 0; column < ScriptTableColumnCount; column++)
        {
            const int cell_index = row * ScriptTableColumnCount + column;
            table->setItem(row, column, new QTableWidgetItem(scripts.at(cell_index)));
        }
    }
}
}
