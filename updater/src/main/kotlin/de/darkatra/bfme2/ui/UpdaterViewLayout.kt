package de.darkatra.bfme2.ui

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxScope
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp

@Composable
fun BoxScope.UpdaterViewLayout(
    actionsSlot: @Composable BoxScope.() -> Unit,
    progressBarSlot: @Composable ColumnScope.() -> Unit,
    leftButtonSlot: @Composable RowScope.() -> Unit,
    rightButtonSlot: @Composable RowScope.() -> Unit
) {

    Box(modifier = Modifier.align(Alignment.TopEnd).padding(10.dp)) {
        actionsSlot()
    }

    Column(
        modifier = Modifier.fillMaxWidth()
            .align(Alignment.BottomCenter)
            .padding(start = 10.dp, end = 10.dp, bottom = 10.dp)
    ) {

        progressBarSlot()

        Spacer(modifier = Modifier.height(5f.dp))

        Row(modifier = Modifier.fillMaxWidth()) {

            leftButtonSlot()

            Spacer(modifier = Modifier.width(5f.dp))

            rightButtonSlot()
        }
    }
}
