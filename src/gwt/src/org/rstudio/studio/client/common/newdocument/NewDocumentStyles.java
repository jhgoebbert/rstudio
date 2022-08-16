/*
 * NewDocumentStyles.java
 *
 * Copyright (C) 2022 by Posit, PBC
 *
 * Unless you have received this program directly from Posit pursuant
 * to the terms of a commercial license agreement with Posit, then
 * this program is licensed to you under the terms of version 3 of the
 * GNU Affero General Public License. This program is distributed WITHOUT
 * ANY EXPRESS OR IMPLIED WARRANTY, INCLUDING THOSE OF NON-INFRINGEMENT,
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE. Please refer to the
 * AGPL (http://www.gnu.org/licenses/agpl-3.0.txt) for more details.
 *
 */


package org.rstudio.studio.client.common.newdocument;

import com.google.gwt.resources.client.CssResource;

public interface NewDocumentStyles extends CssResource
{
   String defaultOutputLabel();
   String formatPanel();
   String fillHeight();
   String optionsIcon();
   String outputFormat();
   String outputFormatName();
   String outputFormatChoice();
   String outputFormatDetails();
   String outputFormatIcon();
   String shinyInfoHeader();
   String shinyInfoPanel();
   String templateDetails();
   String templateList();
   String templatePanel();
   String textBox();
   String textCol();
   String topLabel();
}

