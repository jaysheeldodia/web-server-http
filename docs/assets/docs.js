// JavaScript for enhanced documentation functionality

document.addEventListener('DOMContentLoaded', function() {
    // Initialize documentation features
    initializeNavigation();
    initializeSearch();
    initializeTheme();
    initializeAccessibility();
    
    console.log('📚 C++ WebServer Documentation loaded successfully');
});

// Navigation enhancement
function initializeNavigation() {
    // Smooth scrolling for anchor links
    document.querySelectorAll('a[href^="#"]').forEach(anchor => {
        anchor.addEventListener('click', function (e) {
            e.preventDefault();
            const target = document.querySelector(this.getAttribute('href'));
            if (target) {
                target.scrollIntoView({
                    behavior: 'smooth',
                    block: 'start'
                });
            }
        });
    });
    
    // Active section highlighting
    const observerOptions = {
        rootMargin: '-20% 0px -80% 0px',
        threshold: 0
    };
    
    const observer = new IntersectionObserver(function(entries) {
        entries.forEach(entry => {
            if (entry.isIntersecting) {
                updateActiveSection(entry.target.id);
            }
        });
    }, observerOptions);
    
    document.querySelectorAll('.content-section').forEach(section => {
        observer.observe(section);
    });
}

// Update active section in navigation
function updateActiveSection(sectionId) {
    // Remove active class from all links
    document.querySelectorAll('.sidebar a').forEach(link => {
        link.classList.remove('active');
    });
    
    // Add active class to current section link
    const activeLink = document.querySelector(`.sidebar a[href="#${sectionId}"]`);
    if (activeLink) {
        activeLink.classList.add('active');
    }
}

// Simple search functionality
function initializeSearch() {
    // Create search box if it doesn't exist
    if (!document.querySelector('.search-box')) {
        const searchBox = document.createElement('div');
        searchBox.className = 'search-box';
        searchBox.innerHTML = `
            <input type="text" id="docSearch" placeholder="Search documentation..." 
                   style="width: 100%; padding: 8px 12px; background: #484848; border: 1px solid #666; 
                          border-radius: 6px; color: #fff; margin-bottom: 20px;">
        `;
        
        const sidebar = document.querySelector('.sidebar');
        if (sidebar) {
            sidebar.insertBefore(searchBox, sidebar.firstChild);
        }
    }
    
    // Search functionality
    const searchInput = document.getElementById('docSearch');
    if (searchInput) {
        searchInput.addEventListener('input', function() {
            const query = this.value.toLowerCase().trim();
            filterDocumentation(query);
        });
    }
}

// Filter documentation based on search query
function filterDocumentation(query) {
    const sections = document.querySelectorAll('.content-section');
    const sidebarLinks = document.querySelectorAll('.sidebar a');
    
    if (!query) {
        // Show all sections and links
        sections.forEach(section => section.style.display = 'block');
        sidebarLinks.forEach(link => link.style.display = 'block');
        return;
    }
    
    sections.forEach(section => {
        const content = section.textContent.toLowerCase();
        const isMatch = content.includes(query);
        section.style.display = isMatch ? 'block' : 'none';
    });
    
    sidebarLinks.forEach(link => {
        const text = link.textContent.toLowerCase();
        const isMatch = text.includes(query);
        link.style.display = isMatch ? 'block' : 'none';
        
        // Hide parent li if link is hidden
        if (link.parentElement) {
            link.parentElement.style.display = isMatch ? 'block' : 'none';
        }
    });
}

// Theme and appearance management
function initializeTheme() {
    // Check for system preference
    if (window.matchMedia && window.matchMedia('(prefers-color-scheme: light)').matches) {
        // User prefers light theme, but we keep dark theme as specified
        console.log('System prefers light theme, but documentation uses dark theme as designed');
    }
    
    // Add theme toggle if needed (currently not implemented to maintain design consistency)
    // Could be added in future versions
}

// Accessibility enhancements
function initializeAccessibility() {
    // Add skip link
    const skipLink = document.createElement('a');
    skipLink.href = '#main-content';
    skipLink.className = 'skip-link';
    skipLink.textContent = 'Skip to main content';
    document.body.insertBefore(skipLink, document.body.firstChild);
    
    // Add ARIA labels to navigation
    const sidebar = document.querySelector('.sidebar');
    if (sidebar) {
        sidebar.setAttribute('role', 'navigation');
        sidebar.setAttribute('aria-label', 'Documentation navigation');
    }
    
    const mainContent = document.querySelector('.main-content');
    if (mainContent) {
        mainContent.setAttribute('id', 'main-content');
        mainContent.setAttribute('role', 'main');
    }
    
    // Enhance keyboard navigation
    document.addEventListener('keydown', function(e) {
        // Alt + / to focus search
        if (e.altKey && e.key === '/') {
            e.preventDefault();
            const searchInput = document.getElementById('docSearch');
            if (searchInput) {
                searchInput.focus();
            }
        }
        
        // Escape to clear search
        if (e.key === 'Escape') {
            const searchInput = document.getElementById('docSearch');
            if (searchInput && document.activeElement === searchInput) {
                searchInput.value = '';
                filterDocumentation('');
                searchInput.blur();
            }
        }
    });
}

// Copy code functionality
function addCodeCopyButtons() {
    document.querySelectorAll('pre code').forEach((codeBlock, index) => {
        const button = document.createElement('button');
        button.className = 'copy-button';
        button.textContent = 'Copy';
        button.style.cssText = `
            position: absolute;
            top: 8px;
            right: 8px;
            background: #484848;
            color: #ffd700;
            border: 1px solid #666;
            border-radius: 4px;
            padding: 4px 8px;
            font-size: 12px;
            cursor: pointer;
        `;
        
        button.addEventListener('click', function() {
            navigator.clipboard.writeText(codeBlock.textContent).then(() => {
                button.textContent = 'Copied!';
                setTimeout(() => {
                    button.textContent = 'Copy';
                }, 2000);
            });
        });
        
        const pre = codeBlock.parentElement;
        pre.style.position = 'relative';
        pre.appendChild(button);
    });
}

// Initialize code copy buttons after DOM is loaded
document.addEventListener('DOMContentLoaded', function() {
    setTimeout(addCodeCopyButtons, 100);
});

// Export functions for potential external use
window.WebServerDocs = {
    showSection: showSection,
    filterDocumentation: filterDocumentation,
    updateActiveSection: updateActiveSection
};
